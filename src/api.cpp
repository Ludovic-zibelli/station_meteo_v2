
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <time.h>
#include <Preferences.h>

#include "api.h"
#include "db_read.h"     // fallback unique si NVS vide
#include "log.h"
#include "version.h"

// ---------- Déclarations externes depuis main.cpp ----------
extern int degret; // direction live 0..359

// Modules runtime
extern int module_bmp280, module_dht22, module_anemo, module_girou,
           module_pluvio, module_tension, module_bitvie, module_sht40;
// États capteurs runtime
extern int etat_bmp280, etat_dht22, etat_anemo, etat_girou, etat_pluvio;


// ---------- Variables de conf API (runtime) ----------
static String API_URL;                 // ex: https://.../api/stationdirect/{ID}
static String API_KEY;                 // token
static String API_URL_ETATSTATION;     // ex: https://.../api/etatstationmeteo/1
static int    STATION_ID = 0;
static String STATION_METEOS_PATH;     // ex: api/station_meteos/{ID}
static String API_URL_STATION_METEOS;  // ex: https://.../api/station_meteos/{ID}

// Retours d’envoi
PushResult g_lastPushRow;
PushResult g_lastPushEtat;
int g_lastPushHttpCode = 0;
String g_lastPushHttpBody;
unsigned long g_lastPushMillis = 0;

// Cache d’infos station
StationInfo g_stationInfo;
unsigned long g_lastFetchStationInfo = 0;
const unsigned long FETCH_INFO_MS = 10UL * 60UL * 1000UL; // 10 min

// Snapshot (défini dans main.cpp)
extern StationSnapshot g_snap;


// ---------- Helpers -----------
static inline String rtrimSlash(String s) {
  while (s.endsWith("/")) s.remove(s.length()-1);
  return s;
}

static void recordPushResult(PushResult &dst, const char* endpoint, int code, const String &body) {
  dst.endpoint = endpoint ? String(endpoint) : String();
  dst.code = code;
  dst.body = body;
  dst.ts_ms = millis();
}

static inline float roundN(float v, uint8_t d) {
  if (isnan(v)) return v;
  float p = powf(10.f, d);
  return roundf(v * p) / p;
}

// "YYYY-MM-DD HH:MM:SS" -> "YYYY-MM-DDTHH:MM:SSZ"
static String toISO8601Z(const String& tsSqlite) {
  if (tsSqlite.length() < 19) return String("");
  String s = tsSqlite;
  s.replace(' ', 'T'); // espace -> T
  s.trim();
  if (s.endsWith("Z")) return s;
  return s + "Z";
}

// Ajoute ".000Z" si pas de millis
static String ensureMillisZ(const String& iso) {
  if (iso.endsWith("Z") && iso.length() == 20) {
    String s = iso;
    s.remove(s.length()-1);
    s += ".000Z";
    return s;
  }
  return iso;
}

// "now" ISO (UTC) de secours si pas de datetime snapshot
static String nowISOZ() {
  time_t t = time(nullptr);
  if (t <= 0) return String("1970-01-01T00:00:00.000Z");
  struct tm tmUtc;
  gmtime_r(&t, &tmUtc);
  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
           tmUtc.tm_year + 1900, tmUtc.tm_mon + 1, tmUtc.tm_mday,
           tmUtc.tm_hour, tmUtc.tm_min, tmUtc.tm_sec);
  return String(buf);
}

// Normaliser un angle entier 0..359
static inline int normDegInt(float deg) {
  if (isnan(deg)) return 0;
  int g = (int)floorf(deg + 0.5f);
  g = (g % 360 + 360) % 360;
  return g;
}

// Pour l’API : nombre avec virgule (si besoin)
static String frenchDecimalStr(float v, uint8_t digits = 2) {
  char buf[24]; dtostrf(v, 0, digits, buf);
  String s(buf); s.replace('.', ',');
  return s;
}


// ---------- Config API en NVS (fallback DB une seule fois) ----------
static Preferences prefsApi;

static bool loadApiConfigFromNvs() {
  prefsApi.begin("api", /*ro=*/true);
  bool ok = prefsApi.isKey("ver");
  String base = prefsApi.getString("base", "");
  API_KEY    = prefsApi.getString("key",  "");
  STATION_ID = prefsApi.getInt   ("id",   0);
  prefsApi.end();
  if (!ok || base.isEmpty() || STATION_ID <= 0) return false;

  base = rtrimSlash(base);
  API_URL = base + "/stationdirect/" + String(STATION_ID);
  API_URL_ETATSTATION = base + "/etatstationmeteo/1";
  API_URL_STATION_METEOS = base + "/station_meteos/" + String(STATION_ID);
  STATION_METEOS_PATH    = "api/station_meteos/" + String(STATION_ID);

  Serial.printf("[API] NVS base='%s'\n", base.c_str());
  Serial.printf("[API] stationdirect='%s'\n", API_URL.c_str());
  Serial.printf("[API] etatstation='%s'\n", API_URL_ETATSTATION.c_str());
  Serial.printf("[API] station_meteos='%s'\n", API_URL_STATION_METEOS.c_str());
  return true;
}

static void saveApiConfigToNvs(const String& baseUrl, const String& key, int stationId) {
  prefsApi.begin("api", /*rw=*/false);
  prefsApi.putUChar("ver", 1);
  prefsApi.putString("base", rtrimSlash(baseUrl));
  prefsApi.putString("key",  key);
  prefsApi.putInt   ("id",   stationId);
  
  // Tracker la version du schéma de config et la date de modification
  prefsApi.putUChar("schema_ver", CONFIG_SCHEMA_VERSION);
  prefsApi.putLong("config_ts", time(nullptr)); // timestamp de la dernière modif
  
  prefsApi.end();
}

// Exposée pour que main.cpp puisse aussi y faire appel après /config.api
void apiRefreshConfigFromDb() {
  // 1) Essayer NVS d'abord
  if (loadApiConfigFromNvs()) return;

  // 2) Fallback: lire la DB une seule fois pour ensemencer la NVS
  AppConfig c;
  if (!readAppConfig(c)) {
    Serial.println("[API] readAppConfig() KO");
    return;
  }
  API_KEY    = c.token;
  STATION_ID = String(c.id_station).toInt();
  String base = rtrimSlash(c.adresse_api);
  API_URL = base + "/stationdirect/" + String(STATION_ID);
  API_URL_ETATSTATION = base + "/etatstationmeteo/1";
  API_URL_STATION_METEOS = base + "/station_meteos/" + String(STATION_ID);
  STATION_METEOS_PATH    = "api/station_meteos/" + String(STATION_ID);

  // Enregistrer en NVS pour les boots suivants
  saveApiConfigToNvs(base, API_KEY, STATION_ID);

  Serial.printf("[API] base='%s'\n", base.c_str());
  Serial.printf("[API] stationdirect='%s'\n", API_URL.c_str());
  Serial.printf("[API] etatstation='%s'\n", API_URL_ETATSTATION.c_str());
  Serial.printf("[API] station_meteos='%s'\n", API_URL_STATION_METEOS.c_str());
}


// ---------- Mise à jour Wi‑Fi locale ----------
static void updateWifiInfo() {
  int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -127;
  g_stationInfo.wifi_rssi = rssi;

  // % approx
  int pct;
  if (rssi <= -100) pct = 0;
  else if (rssi >= -50) pct = 100;
  else pct = map(rssi, -100, -50, 0, 100);
  g_stationInfo.wifi_percent = constrain(pct, 0, 100);

  // barres 0..4
  int bars = 0;
  if      (rssi >= -60) bars = 4;
  else if (rssi >= -70) bars = 3;
  else if (rssi >= -80) bars = 2;
  else if (rssi >= -90) bars = 1;
  else bars = 0;
  g_stationInfo.wifi_bars = bars;
}


// ========== Payload JSON RAM-only pour /api/localStationInfo ==========
String buildStationJsonPayload() {
  updateWifiInfo();

  StaticJsonDocument<1536> doc;

  auto round2 = [](float v)->float {
    return isnan(v) ? NAN : roundf(v * 100.0f) / 100.0f;
  };

  // Mesures live (snapshot)
  if (!isnan(g_snap.temp_dht))   doc["temperature"]   = round2(g_snap.temp_dht);
  if (!isnan(g_snap.hum))        doc["humidite"]      = round2(g_snap.hum);
  if (!isnan(g_snap.press_hPa))  doc["pression"]      = round2(g_snap.press_hPa);
  if (!isnan(g_snap.temp_bmp))   doc["tempbmp280"]    = round2(g_snap.temp_bmp);
  if (g_snap.datetime[0])        doc["datetime"]      = g_snap.datetime;
  // uptime en secondes (entier)
  doc["tpsvie"] = (unsigned long)(millis() / 1000UL);
  if (!isnan(g_snap.dew))        doc["pointderosee"]  = round2(g_snap.dew);
  if (!isnan(g_snap.wind))       doc["anemometre"]    = round2(g_snap.wind);
  if (!isnan(g_snap.rain_cum))   doc["pluviometre"]   = round2(g_snap.rain_cum);
  if (!isnan(g_snap.gust))       doc["rafale"]        = round2(g_snap.gust);
  if (degret >= 0 && degret < 360) doc["girouette"]   = degret;

  // Tensions
  if (!isnan(g_snap.solar_v))    doc["tension_solaire"]  = round2(g_snap.solar_v);
  if (!isnan(g_snap.batt_v))     doc["tension_batterie"] = round2(g_snap.batt_v);

  // Wi‑Fi
  doc["wifi_rssi"]    = g_stationInfo.wifi_rssi;
  doc["wifi_percent"] = g_stationInfo.wifi_percent;
  doc["wifi_bars"]    = g_stationInfo.wifi_bars;

  // Meta
  doc["stationMeteo"] = String("/") + STATION_METEOS_PATH;
  doc["last_push_code"] = g_lastPushHttpCode;
  if (g_lastPushHttpBody.length()) doc["last_push_body"] = g_lastPushHttpBody;
  doc["last_push_age_ms"] = (unsigned long)(millis() - g_lastPushMillis);

  doc["last_push_row_code"] = g_lastPushRow.code;
  if (g_lastPushRow.body.length()) doc["last_push_row_body"] = g_lastPushRow.body;
  doc["last_push_row_age_ms"] = (unsigned long)(millis() - g_lastPushRow.ts_ms);
  doc["last_push_row_endpoint"] = g_lastPushRow.endpoint;

  doc["last_push_etat_code"] = g_lastPushEtat.code;
  if (g_lastPushEtat.body.length()) doc["last_push_etat_body"] = g_lastPushEtat.body;
  doc["last_push_etat_age_ms"] = (unsigned long)(millis() - g_lastPushEtat.ts_ms);
  doc["last_push_etat_endpoint"] = g_lastPushEtat.endpoint;

  // Version info
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["config_schema_version"] = CONFIG_SCHEMA_VERSION;
  
  // Récupérer timestamp de dernière modif config depuis NVS
  Preferences prefsVer; prefsVer.begin("api", true);
  long config_ts = prefsVer.getLong("config_ts", 0);
  prefsVer.end();
  if (config_ts > 0) doc["config_last_modified"] = config_ts;

  String payload; serializeJson(doc, payload);
  return payload;
}


// ========== Payload STRICT /stationdirect (RAM-only) ==========
String buildStationJsonPayloadStrict() {
  StaticJsonDocument<1024> doc;

  // dateheure ISO à partir du snapshot si présent, sinon now()
  if (g_snap.datetime[0]) doc["dateheure"] = ensureMillisZ(toISO8601Z(String(g_snap.datetime)));
  else                    doc["dateheure"] = nowISOZ();

  // Mesures (types alignés)
  if (!isnan(g_snap.temp_dht))   doc["tempdh22"]     = roundN(g_snap.temp_dht, 1);
  if (!isnan(g_snap.temp_bmp))   doc["tempbmp280"]   = roundN(g_snap.temp_bmp, 1);
  if (!isnan(g_snap.hum))        doc["humidite"]     = (int)roundf(g_snap.hum);     // entier
  if (!isnan(g_snap.press_hPa))  doc["pression"]     = roundN(g_snap.press_hPa, 1);
  if (!isnan(g_snap.wind))       doc["anemometre"]   = roundN(g_snap.wind, 1);
  if (!isnan(g_snap.rain_cum))   doc["pluviometre"]  = roundN(g_snap.rain_cum, 2);
  // Girouette int 0..359
  doc["girouette"] = normDegInt((float)degret);
  // Point de rosée attendu STRING "12,34"
  if (!isnan(g_snap.dew))        doc["pointRose"]    = frenchDecimalStr(g_snap.dew, 2);
  // tpsvie attendu STRING
  doc["tpsvie"] = String((unsigned long)(millis() / 1000UL));

  // Champs “Météo France” (vides par défaut)
  doc["Eclaire1km"] = 0;
  doc["Eclaire10km"] = 0;
  doc["Eclaire50km"] = 0;
  doc["alertemeteofrance"] = "";
  doc["datedebutmeteofrance"] = nowISOZ();
  doc["datefinmeteofrance"]   = nowISOZ();

  // Ghost
  doc["ghost"] = 0;

  // Station : ID + chemin relatif
  doc["stationId"]   = STATION_ID;
  doc["stationMeteos"] = String("/") + STATION_METEOS_PATH;

  String out; serializeJson(doc, out);
  return out;
}


// ========== Handlers/Tasks ==========

void setupLocalStationApiHandler(WebServer &server) {
  server.on("/api/localStationInfo", HTTP_GET, [&server]() {
    String payload = buildStationJsonPayload();
    server.send(200, "application/json", payload);
  });
}


// TaskHandle pour que main.cpp puisse la suspendre/reprendre
TaskHandle_t stationInfoTaskHandle = NULL;

static void stationInfoTask(void *pv) {
  (void)pv;
  for (;;) {
    // Attente coopérative pendant OTA
    extern volatile bool otaInProgress;
    while (otaInProgress) vTaskDelay(pdMS_TO_TICKS(500));
    // fetch distant si voulu (tu peux garder ou ignorer)
    fetchStationInfoFromRemote();
    vTaskDelay(pdMS_TO_TICKS(FETCH_INFO_MS));
  }
  vTaskDelete(NULL);
}

void startStationInfoBackgroundTask() {
  xTaskCreatePinnedToCore(
    stationInfoTask, "stationInfo",
    8192, NULL, 1, &stationInfoTaskHandle, 0
  );
}

void maybeRefreshStationInfo() {
  if (millis() - g_lastFetchStationInfo > FETCH_INFO_MS || !g_stationInfo.valid) {
    if (!fetchStationInfoFromRemote()) updateWifiInfo();
  } else {
    updateWifiInfo();
  }
}

bool fetchStationInfoFromRemote() {
  if (API_URL_STATION_METEOS.isEmpty()) return false;
  if (WiFi.status() != WL_CONNECTED)    return false;

  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, API_URL_STATION_METEOS)) return false;
  http.addHeader("Accept", "application/json");
  if (API_KEY.length()) http.addHeader("X-API-KEY", API_KEY);
  int code = http.GET();
  if (code != 200) { http.end(); return false; }

  String body = http.getString();
  http.end();

  StaticJsonDocument<1024> doc;
  auto err = deserializeJson(doc, body);
  if (err) return false;

  g_stationInfo.id         = doc["id"]         | 0;
  g_stationInfo.ville      = (const char*)(doc["ville"]      | "");
  g_stationInfo.codepostal = doc["codepostal"] | 0;
  g_stationInfo.description= (const char*)(doc["description"]| "");
  g_stationInfo.diy        = doc["diy"]        | false;

  JsonObject u = doc["user"].as<JsonObject>();
  if (!u.isNull()) {
    g_stationInfo.user_id   = u["id"]    | 0;
    g_stationInfo.user_nom  = (const char*)(u["nom"]    | "");
    g_stationInfo.user_prenom=(const char*)(u["prenom"] | "");
    g_stationInfo.user_email= (const char*)(u["email"]  | "");
  }
  g_stationInfo.valid = true;
  g_lastFetchStationInfo = millis();
  return true;
}


// ========== Envois HTTP ==========

// /stationdirect (strict)
bool sendLatestRowToApi() {
  if (API_URL.isEmpty()) {
    recordPushResult(g_lastPushRow, "cfg", -10, "No API config");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    recordPushResult(g_lastPushRow, API_URL.c_str(), -2, "No WiFi");
    return false;
  }

  String payload = buildStationJsonPayloadStrict();
  Serial.printf("[API] stationdirect PUT len=%u\n", (unsigned)payload.length());
  app_logf("[API] stationdirect PUT len=%u\n", (unsigned)payload.length());

  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, API_URL)) {
    recordPushResult(g_lastPushRow, API_URL.c_str(), -1, "http.begin() failed");
    return false;
  }

  http.setConnectTimeout(8000);
  http.setTimeout(12000);
  http.setReuse(false);
  http.addHeader("Content-Type", "application/json; charset=utf-8");
  http.addHeader("Accept", "application/json");
  if (API_KEY.length()) http.addHeader("X-API-KEY", API_KEY);

  int code = http.PUT(payload);
  String resp = http.getString();
  http.end();

  recordPushResult(g_lastPushRow, API_URL.c_str(), code, resp);
  g_lastPushHttpCode  = code;
  g_lastPushHttpBody  = resp;
  g_lastPushMillis    = millis();

  Serial.printf("[API] stationdirect -> code=%d\n", code);
  app_logf("[API] stationdirect -> code=%d\n", code);
  if (resp.length()) app_logf("%s", resp.c_str());

  return (code >= 200 && code < 300);
}

// /etatstationmeteo (modules + états + tensions) — RAM-only
bool sendLatestEtatStationMeteoToApi() {
  if (API_URL_ETATSTATION.isEmpty()) {
    recordPushResult(g_lastPushEtat, "cfg", -10, "No API config");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    recordPushResult(g_lastPushEtat, API_URL_ETATSTATION.c_str(), -2, "No WiFi");
    return false;
  }

  StaticJsonDocument<1024> doc;
  // Modules (runtime)
  doc["moduleBmp280"] = module_bmp280;
  doc["moduleDht22"]  = module_dht22;
  doc["moduleAnemo"]  = module_anemo;
  doc["moduleGirou"]  = module_girou;
  doc["modulePluvio"] = module_pluvio;
  doc["moduleTension"]= module_tension;
  doc["moduleBitvie"] = module_bitvie;

  // États (runtime)
  doc["capteurDht22"]  = etat_dht22;
  doc["capteurBmp280"] = etat_bmp280;
  doc["capteurPluvio"] = etat_pluvio;
  doc["capteurGirou"]  = etat_girou;
  doc["capteurAnemo"]  = etat_anemo;

  // Ghost
  doc["ghost"] = 0;

  // Tensions depuis snapshot RAM
  if (!isnan(g_snap.solar_v)) doc["tensionSolaire"]  = g_snap.solar_v;
  if (!isnan(g_snap.batt_v))  doc["tensionBatterie"] = g_snap.batt_v;

  String payload; serializeJson(doc, payload);

  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, API_URL_ETATSTATION)) {
    recordPushResult(g_lastPushEtat, API_URL_ETATSTATION.c_str(), -1, "http.begin() failed");
    return false;
  }

  http.setConnectTimeout(8000);
  http.setTimeout(12000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  if (API_KEY.length()) http.addHeader("X-API-KEY", API_KEY);

  int code = http.PUT(payload);
  String resp = http.getString();
  http.end();

  recordPushResult(g_lastPushEtat, API_URL_ETATSTATION.c_str(), code, resp);
  g_lastPushHttpCode = code;
  g_lastPushHttpBody = resp;
  g_lastPushMillis   = millis();

  Serial.printf("[API] PUT %s -> code %d\n", API_URL_ETATSTATION.c_str(), code);
  app_logf("[API] PUT %s -> code %d\n", API_URL_ETATSTATION.c_str(), code);
  if (resp.length()) app_logf("%s", resp.c_str());

  return (code >= 200 && code < 300);
}
