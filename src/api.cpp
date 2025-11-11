#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebServer.h> 
#include "db_read.h"
#include "api.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


// Config API dynamique
static AppConfig g_apiCfg;
static unsigned long g_apiCfgLoadedMs = 0;
static const unsigned long API_CFG_TTL_MS = 60 * 1000UL;



// === Variables "runtime" avec les mêmes noms qu'avant ===
String API_URL;                   // https://.../api/stationdirect/{STATION_ID}
String API_KEY;                   // X-API-KEY depuis DB
String API_URL_ETATSTATION;       // https://.../api/etatstationmeteo/1  (ID=1 imposé)
int    STATION_ID = 0;            // entier (ID_STATION parsé)
String STATION_METEOS_PATH;       // api/station_meteos/{STATION_ID}
String API_URL_STATION_METEOS;    // https://.../api/station_meteos/{STATION_ID}

// utilitaire
static String rtrimSlash(String s){ while (s.endsWith("/")) s.remove(s.length()-1); return s; }

// Appeler cette fonction AU DÉMARRAGE et APRÈS /config.save
void apiRefreshConfigFromDb() {
  AppConfig c;
  if (!readAppConfig(c)) {         // lit table config id=1  [1](https://shiftup-my.sharepoint.com/personal/u058770_inetpsa_com/Documents/Fichiers%20Microsoft%20Copilot%20Chat/api.cpp)
    Serial.println("[API] readAppConfig() KO");
    return;
  }

  // Token
  API_KEY = c.token;

  // ID station en ENTIER (si "2.0" en base -> 2)
  STATION_ID = String(c.id_station).toInt();

  // Base URL genre "https://www.meteospit.fr/api"
  String base = rtrimSlash(c.adresse_api);

  // Endpoints construits
  API_URL                 = base + "/stationdirect/"     + String(STATION_ID);
  API_URL_ETATSTATION     = base + "/etatstationmeteo/1";                 // ID = 1 imposé
  API_URL_STATION_METEOS  = base + "/station_meteos/"    + String(STATION_ID);
  STATION_METEOS_PATH     = "api/station_meteos/"        + String(STATION_ID);

  Serial.printf("[API] cfg ok: STATION_ID=%d\n", STATION_ID);
}


// S'assure que la config est chargée; la recharge si plus vieille que TTL
bool ensureApiConfig() {
    apiRefreshConfigFromDb(); // exécute la mise à jour
    return true; // indique que tout s'est bien passé
}


/*
// Remplace par les tiens
static const char* API_URL  = "https://www.meteospit.fr/api/stationdirect/2";
static const char* API_KEY  = "2878ece33344e4f6d9e1105c0362f0671d9432fb4d997023acb734f4c6e6793a";
static const char* API_URL_ETATSTATION = "https://www.meteospit.fr/api/etatstationmeteo/1";
static const int   STATION_ID = 2;
static const char* STATION_METEOS_PATH = "api/station_meteos/2";
static const char* API_URL_STATION_METEOS = "https://www.meteospit.fr/api/station_meteos/2";
*/
// utiliser la structure et l'extern définis dans include/api.h
// (supprimer la struct StationInfo locale et la variable static ci‑dessous)

StationInfo g_stationInfo;
unsigned long g_lastFetchStationInfo = 0;
const unsigned long FETCH_INFO_MS = 10 * 60 * 1000UL; // 10 minutes

Measurements g_measurements;

// définition des globals
PushResult g_lastPushRow;
PushResult g_lastPushEtat;

// définitions des globals utilisés par buildStationJsonPayload()
int g_lastPushHttpCode = 0;
String g_lastPushHttpBody = "";
unsigned long g_lastPushMillis = 0;

// helper pour enregistrer résultat
static void recordPushResult(PushResult &dst, const char* endpoint, int code, const String &body) {
  dst.endpoint = endpoint ? String(endpoint) : String();
  dst.code = code;
  dst.body = body;
  dst.ts_ms = millis();
}

// Transforme "YYYY-MM-DD HH:MM:SS" -> "YYYY-MM-DDTHH:MM:SS+00:00"
static String toISO8601(const String& tsSqlite) {
  if (tsSqlite.length() < 19) return String(""); // fallback si vide
  String s = tsSqlite;
  s.replace(" ", "T");
  return s + "+00:00"; // adapte si tu veux le fuseau local
}

// Convertit 12.34 -> "12,34" (ton API veut une virgule pour pointRose)
static String frenchDecimal(float v, uint8_t digits = 1) {
  char buf[24];
  dtostrf(v, 0, digits, buf);
  String s(buf);
  s.replace('.', ',');
  return s;
}

// Lis la dernière ligne de station_direct et l’envoie à l’API
bool sendLatestRowToApi() {
  if (API_URL.isEmpty()) {  // config pas encore chargée
    recordPushResult(g_lastPushRow, "cfg", -10, "No API config");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    recordPushResult(g_lastPushRow, API_URL.c_str(), -2, "No WiFi");  // <-- .c_str()
    return false;
  }

  String payload = buildStationJsonPayload();
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, API_URL)) {
    recordPushResult(g_lastPushRow, API_URL.c_str(), -1, "http.begin() failed");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  if (API_KEY.length()) http.addHeader("X-API-KEY", API_KEY);

  int code = http.PUT(payload);
  String resp = http.getString();
  http.end();
  recordPushResult(g_lastPushRow, API_URL.c_str(), code, resp);
  return (code >= 200 && code < 300);
}

bool sendEtatCapteursToApi(
    const Modules& mods,
    int etat_dht22, int etat_bmp280, int etat_pluvio, int etat_girou, int etat_anemo, int ghost,
    String logDateBmp280, String logBmp280,
    String logDateDht22, String logDht22,
    String logDateGirou, String logGirou,
    String logDateTension, String logTension,
    String logDateAnemo, String logAnemo,
    String logDatePluvio, String logPluvio
) {
    // 0) Vérif config API en mémoire (chargée via apiRefreshConfigFromDb())
    if (API_URL_ETATSTATION.isEmpty() || STATION_METEOS_PATH.isEmpty()) {
        recordPushResult(g_lastPushEtat, "cfg", -10, "No API config");
        return false;
    }

    // 1) Vérif Wi‑Fi
    if (WiFi.status() != WL_CONNECTED) {
        recordPushResult(g_lastPushEtat, API_URL_ETATSTATION.c_str(), -2, "No WiFi");
        return false;
    }

    // 2) Construire le payload JSON
    StaticJsonDocument<512> doc;
    doc["moduleBmp280"]   = mods.bmp280;
    doc["moduleDht22"]    = mods.dht22;
    doc["moduleAnemo"]    = mods.anemo;
    doc["moduleGirou"]    = mods.girou;
    doc["modulePluvio"]   = mods.pluvio;
    doc["moduleTension"]  = mods.tension;
    doc["moduleBitvie"]   = mods.bitvie;

    doc["capteurDht22"]   = etat_dht22;
    doc["capteurBmp280"]  = etat_bmp280;
    doc["capteurPluvio"]  = etat_pluvio;
    doc["capteurGirou"]   = etat_girou;
    doc["capteurAnemo"]   = etat_anemo;

    doc["ghost"]          = ghost;

    doc["logDateBmp280"]  = logDateBmp280;
    doc["logBmp280"]      = logBmp280;
    doc["logDateDht22"]   = logDateDht22;
    doc["logDht22"]       = logDht22;
    doc["logDateGirou"]   = logDateGirou;
    doc["logGirou"]       = logGirou;
    doc["logDateTension"] = logDateTension;
    doc["logTension"]     = logTension;
    doc["logDateAnemo"]   = logDateAnemo;
    doc["logAnemo"]       = logAnemo;
    doc["logDatePluvio"]  = logDatePluvio;
    doc["logPluvio"]      = logPluvio;

    // ⚠️ IMPORTANT : utiliser le chemin relatif dynamique
    // ex : "/api/station_meteos/2"
    doc["stationMeteo"]   = String("/") + STATION_METEOS_PATH;

    String payload;
    serializeJson(doc, payload);

    // 3) Appel HTTP
    WiFiClientSecure client; 
    client.setInsecure(); // garde comme dans ton code
    HTTPClient http;

    if (!http.begin(client, API_URL_ETATSTATION)) { // URL avec ID fixé à 1 côté serveur
        recordPushResult(g_lastPushEtat, API_URL_ETATSTATION.c_str(), -1, "http.begin() failed");
        return false;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    if (API_KEY.length()) {
        http.addHeader("X-API-KEY", API_KEY); // token issu de la DB
    }

    int code = http.PUT(payload);
    String resp = http.getString();
    http.end();

    // 4) Journaliser le résultat pour tes handlers/local info
    recordPushResult(g_lastPushEtat, API_URL_ETATSTATION.c_str(), code, resp);
    g_lastPushHttpCode = code;
    g_lastPushHttpBody = resp;
    g_lastPushMillis   = millis();

    return (code >= 200 && code < 300);
}

bool sendLatestEtatStationMeteoToApi() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[API] WiFi non connecté");
        recordPushResult(g_lastPushEtat, API_URL_ETATSTATION.c_str(), -2, "No WiFi");
        return false;
    }

    EtatCapteurs ec;
    if (!readLatestEtatCapteurs(ec)) {
        Serial.println("[API] Impossible de lire etatcapteurs");
        recordPushResult(g_lastPushEtat, API_URL_ETATSTATION.c_str(), -3, "Read etatcapteurs failed");
        return false;
    }

    StaticJsonDocument<1024> doc;
    doc["moduleBmp280"]   = ec.module_bmp280;
    doc["moduleDht22"]    = ec.module_dht22;
    doc["moduleAnemo"]    = ec.module_anemo;
    doc["moduleGirou"]    = ec.module_girou;
    doc["modulePluvio"]   = ec.module_pluvio;
    doc["moduleTension"]  = ec.module_tension;
    doc["moduleBitvie"]   = ec.module_bitvie;
    doc["capteurDht22"]   = ec.capteur_dht22;
    doc["capteurBmp280"]  = ec.capteur_bmp280;
    doc["capteurPluvio"]  = ec.capteur_pluvio;
    doc["capteurGirou"]   = ec.capteur_girou;
    doc["capteurAnemo"]   = ec.capteur_anemo;
    doc["ghost"]          = 0;
    doc["tensionSolaire"] = ec.tension_solaire;
    doc["tensionBatterie"] = ec.tension_batterie;

    String payload;
    serializeJson(doc, payload);

    Serial.println("Payload JSON envoyé (etatstationmeteo) :");
    Serial.println(payload);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, API_URL_ETATSTATION)) {
        Serial.println("[API] http.begin() a échoué");
        recordPushResult(g_lastPushEtat, API_URL_ETATSTATION.c_str(), -1, "http.begin() failed");
        return false;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    http.addHeader("X-API-KEY", API_KEY);

    int code = http.PUT(payload);
    String resp = http.getString();

    Serial.printf("[API] PUT %s -> code %d\n", API_URL_ETATSTATION, code);
    Serial.println(resp);

    http.end();

    // ✅ Enregistre le résultat
    recordPushResult(g_lastPushEtat, API_URL_ETATSTATION.c_str(), code, resp);
    g_lastPushHttpCode = code;
    g_lastPushHttpBody = resp;
    g_lastPushMillis = millis();

    return code >= 200 && code < 300;
}

bool fetchStationInfoFromRemote() {

  if (API_URL_STATION_METEOS.isEmpty()) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

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

  g_stationInfo.id = doc["id"] | 0;
  g_stationInfo.ville = String((const char*)(doc["ville"] | ""));
  g_stationInfo.codepostal = doc["codepostal"] | 0;
  g_stationInfo.description = String((const char*)(doc["description"] | ""));
  g_stationInfo.diy = doc["diy"] | false;
  JsonObject u = doc["user"].as<JsonObject>();
  if (!u.isNull()) {
    g_stationInfo.user_id = u["id"] | 0;
    g_stationInfo.user_nom = String((const char*)(u["nom"] | ""));
    g_stationInfo.user_prenom = String((const char*)(u["prenom"] | ""));
    g_stationInfo.user_email = String((const char*)(u["email"] | ""));
  }
  g_stationInfo.valid = true;
  g_lastFetchStationInfo = millis();
  return true;
}

static void updateWifiInfo() {
  int rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -127;
  g_stationInfo.wifi_rssi = rssi;
  // pourcentage approx -100..-50 -> 0..100
  int pct;
  if (rssi <= -100) pct = 0;
  else if (rssi >= -50) pct = 100;
  else pct = map(rssi, -100, -50, 0, 100);
  g_stationInfo.wifi_percent = constrain(pct, 0, 100);
  // bars 0..4
  int bars = 0;
  if (rssi >= -60) bars = 4;
  else if (rssi >= -70) bars = 3;
  else if (rssi >= -80) bars = 2;
  else if (rssi >= -90) bars = 1;
  else bars = 0;
  g_stationInfo.wifi_bars = bars;
}

// helper pour construire le JSON à partir du cache (g_stationInfo, g_measurements)
String buildStationJsonPayload() {
  updateWifiInfo();

  StaticJsonDocument<1536> doc;

  // helper : arrondir à 2 décimales (conserve NAN si invalide)
  auto round2 = [](float v) -> float {
    return isnan(v) ? NAN : roundf(v * 100.0f) / 100.0f;
  };

  // station
  doc["id"] = g_stationInfo.id;
  doc["ville"] = g_stationInfo.ville;
  doc["codepostal"] = g_stationInfo.codepostal;
  doc["description"] = g_stationInfo.description;
  doc["diy"] = g_stationInfo.diy;
  JsonObject u = doc.createNestedObject("user");
  u["id"] = g_stationInfo.user_id;
  u["nom"] = g_stationInfo.user_nom;
  u["prenom"] = g_stationInfo.user_prenom;
  u["email"] = g_stationInfo.user_email;

  // essayer de lire la dernière ligne directement depuis la base
  StationDirect sd;
  if (readLatestStationDirect(sd)) {
    // arrondir les nombres à 2 décimales
    if (!isnan(sd.tempdht22))     doc["temperature"]       = round2(sd.tempdht22);
    if (!isnan(sd.humiditer))     doc["humidite"]          = round2(sd.humiditer);
    if (!isnan(sd.pression))      doc["pression"]          = round2(sd.pression);
    if (!isnan(sd.tempbmp280))    doc["tempbmp280"]        = round2(sd.tempbmp280);
    if (sd.timestamp.length())    doc["datetime"]          = sd.timestamp;
    doc["tpsvie"]                = (int)sd.tpsvie;
    if (!isnan(sd.pointderosee))  doc["pointderosee"]      = round2(sd.pointderosee);
    if (!isnan(sd.anemometre))    doc["anemometre"]        = round2(sd.anemometre);
    if (!isnan(sd.pluviometre))   doc["pluviometre"]       = round2(sd.pluviometre);
    if (!isnan(sd.rafale))        doc["rafale"]            = round2(sd.rafale);

    // tensions : elles sont stockées dans etatcapteurs -> lire le dernier enregistrement
    EtatCapteurs ec;
    if (readLatestEtatCapteurs(ec)) {
      if (!isnan(ec.tension_solaire))  doc["tension_solaire"]  = round2(ec.tension_solaire);
      if (!isnan(ec.tension_batterie)) doc["tension_batterie"] = round2(ec.tension_batterie);
    }
  } else {
    // fallback : utiliser cache g_measurements
    if (!isnan(g_measurements.temperature))     doc["temperature"]       = round2(g_measurements.temperature);
    if (!isnan(g_measurements.humidite))        doc["humidite"]          = round2(g_measurements.humidite);
    if (!isnan(g_measurements.pression))        doc["pression"]          = round2(g_measurements.pression);
    if (!isnan(g_measurements.tempbmp280))      doc["tempbmp280"]        = round2(g_measurements.tempbmp280);
    if (g_measurements.datetime.length())       doc["datetime"]          = g_measurements.datetime;
    doc["tpsvie"] = g_measurements.tpsvie;
    if (!isnan(g_measurements.pointderosee))    doc["pointderosee"]      = round2(g_measurements.pointderosee);
    if (!isnan(g_measurements.anemometre))      doc["anemometre"]        = round2(g_measurements.anemometre);
    if (!isnan(g_measurements.pluviometre))     doc["pluviometre"]       = round2(g_measurements.pluviometre);
    if (!isnan(g_measurements.rafale))          doc["rafale"]            = round2(g_measurements.rafale);
    // tensions (fallback)
    if (!isnan(g_measurements.tension_solaire)) {
      doc["tension_solaire"] = round2(g_measurements.tension_solaire);
    } else {
      EtatCapteurs ec;
      if (readLatestEtatCapteurs(ec) && !isnan(ec.tension_solaire)) doc["tension_solaire"] = round2(ec.tension_solaire);
    }

    if (!isnan(g_measurements.tension_batterie)) {
      doc["tension_batterie"] = round2(g_measurements.tension_batterie);
    } else {
      EtatCapteurs ec2;
      if (readLatestEtatCapteurs(ec2) && !isnan(ec2.tension_batterie)) doc["tension_batterie"] = round2(ec2.tension_batterie);
    }
  }

  // wifi (toujours)
  doc["wifi_rssi"] = g_stationInfo.wifi_rssi;
  doc["wifi_percent"] = g_stationInfo.wifi_percent;
  doc["wifi_bars"] = g_stationInfo.wifi_bars;

  // meta
  doc["stationMeteo"] = "/api/station_meteos/2";
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

  String payload;
  serializeJson(doc, payload);
  return payload;
}

// modifier le handler pour utiliser le helper
void setupLocalStationApiHandler(WebServer &server) {
  server.on("/api/localStationInfo", HTTP_GET, [&server]() {
    String payload = buildStationJsonPayload();
    server.send(200, "application/json", payload);
  });

  // ajoute dans setupLocalStationApiHandler ou crée un nouveau handler /data
  server.on("/data", HTTP_GET, [&server]() {
    updateWifiInfo();
    StaticJsonDocument<1024> out;

    // infos station
    out["id"] = g_stationInfo.id;
    out["ville"] = g_stationInfo.ville;
    out["codepostal"] = g_stationInfo.codepostal;
    out["description"] = g_stationInfo.description;
    out["diy"] = g_stationInfo.diy;
    JsonObject u = out.createNestedObject("user");
    u["id"] = g_stationInfo.user_id;
    u["nom"] = g_stationInfo.user_nom;
    u["prenom"] = g_stationInfo.user_prenom;
    u["email"] = g_stationInfo.user_email;

    // mesures : ajouter uniquement si disponibles
    if (!isnan(g_measurements.temperature)) out["temperature"] = g_measurements.temperature;
    else out["temperature"] = nullptr;

    if (!isnan(g_measurements.humidite)) out["humidite"] = g_measurements.humidite;
    else out["humidite"] = nullptr;

    if (!isnan(g_measurements.tempbmp280)) out["tempbmp280"] = g_measurements.tempbmp280;
    else out["tempbmp280"] = nullptr;

    if (!isnan(g_measurements.pression)) out["pression"] = g_measurements.pression;
    else out["pression"] = nullptr;

    if (!isnan(g_measurements.tension_solaire)) out["tension_solaire"] = g_measurements.tension_solaire;
    else out["tension_solaire"] = nullptr;

    if (!isnan(g_measurements.tension_batterie)) out["tension_batterie"] = g_measurements.tension_batterie;
    else out["tension_batterie"] = nullptr;

    out["datetime"] = g_measurements.datetime;
    out["tpsvie"] = g_measurements.tpsvie;

    if (!isnan(g_measurements.pointderosee)) out["pointderosee"] = g_measurements.pointderosee;
    else out["pointderosee"] = nullptr;

    if (!isnan(g_measurements.anemometre)) out["anemometre"] = g_measurements.anemometre;
    else out["anemometre"] = nullptr;

    if (!isnan(g_measurements.rafale)) out["rafale"] = g_measurements.rafale;
    else out["rafale"] = nullptr;

    if (!isnan(g_measurements.pluviometre)) out["pluviometre"] = g_measurements.pluviometre;
    else out["pluviometre"] = nullptr;

    // wifi
    out["wifi_rssi"] = g_stationInfo.wifi_rssi;
    out["wifi_percent"] = g_stationInfo.wifi_percent;
    out["wifi_bars"] = g_stationInfo.wifi_bars;

    String payload;
    serializeJson(out, payload);
    server.send(200, "application/json", payload);
  });
}

// Tâche FreeRTOS pour récupérer les infos de la station en arrière-plan
static void stationInfoTask(void *pvParameters) {
  (void) pvParameters;
  const TickType_t delayTicks = pdMS_TO_TICKS(FETCH_INFO_MS);
  // boucle permanente sur core 0
  for (;;) {
    // fetch (bloquant possible) — ok sur core 0
    fetchStationInfoFromRemote();
    // attendre la prochaine itération
    vTaskDelay(delayTicks);
  }
  // never reached
  vTaskDelete(NULL);
}

// Appeler cette fonction depuis setup() pour lancer la tâche
void startStationInfoBackgroundTask() {
  // taille de pile 8192 bytes, priorité 1, pinned to core 0
  // augmente la pile si tu vois des crashs de stack
  xTaskCreatePinnedToCore(
    stationInfoTask,
    "stationInfo",
    8192,
    NULL,
    1,
    NULL,
    0 // core 0
  );
}

// Appel périodique depuis loop() si besoin
void maybeRefreshStationInfo() {
  if (millis() - g_lastFetchStationInfo > FETCH_INFO_MS || !g_stationInfo.valid) {
    if (!fetchStationInfoFromRemote()) {
      // si fetch échoue, au moins mettre à jour le RSSI local
      updateWifiInfo();
    }
  } else {
    // pas besoin de fetch distant, actualise juste le RSSI
    updateWifiInfo();
  }
}