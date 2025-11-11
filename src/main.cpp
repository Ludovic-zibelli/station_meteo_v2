#include <Arduino.h>
#include <ArduinoJson.h>

//Capteurs
#include <anemometre.h>
#include "bmp280.h"
#include <tensions.h>
#include <SPI.h>
#include <Wire.h>
#include "dht22.h"
#include "pluviometre.h"
#include "girouette.h"

//Stockage des données
#include "SPIFFS.h"


//Serveur web
#include <WiFi.h>
#include "ElegantOTA.h"
#include <WebServer.h>
#include <ArduinoOTA.h>
//#include <ESPAsyncWebServer.h>

//Base de données
#include "bd.h"
#include "bd_mgr.h"

//RTC
#include <RTClib.h>

//Calculs
#include "pointderosee.h"

//Envoi des donnees 
#include "api.h"
#include "db_read.h"
#include "bd.h"




#include <cstring> // IMPORTANT pour memcmp

RTC_DS1307 rtc;

WebServer server(80);

// --- Helpers HTTP pour WebServer synchrone (SPIFFS) ---
String contentTypeFor(const String& path) {
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".css"))  return "text/css";
  if (path.endsWith(".js"))   return "application/javascript";
  if (path.endsWith(".png"))  return "image/png";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".gif"))  return "image/gif";
  if (path.endsWith(".svg"))  return "image/svg+xml";
  if (path.endsWith(".ico"))  return "image/x-icon";
  return "application/octet-stream";
}

void sendWithCache(const String& path) {
  if (!SPIFFS.exists(path)) {
    server.send(404, "text/plain", "Not found");
    return;
  }
  File f = SPIFFS.open(path, "r");
  server.sendHeader("Cache-Control", "public, max-age=2592000, immutable"); // ~30 jours
  server.streamFile(f, contentTypeFor(path));
  f.close();
}

//Variable fonctionement programme
int compteur;
int activation_envoi_api = 0; //1 = envoi des données à l'API activé, 0 = désactivé

//variable donnees capteurs
//BMP280
float temp1;
float pression;
float altitude;

//DHT22
float temp2;
int humiditer;

//Anemometre
int etat;
float vitesse;
float rafale;

//Girouette
int degret;
int etatnord;
int etatsud;
int etatouest;
int etatest;

//Pluviometre
float quantite;

//Tension
int valeur_batterie;
float tension_batterie;
int valeur_solaire;
float tension_solaire;

//Etat capteurs
int etat_bmp280 = 0;
bool bmp_ok = false;
int etat_dht22 = 0;
int etat_anemo = 0;
int etat_girou = 0;
int etat_pluvio = 0;

//Activation des capteurs
int module_bmp280 = 1; //1 = BMP280 activé, 0 = désactivé
int module_dht22 = 1;  //1 = DHT22 activé, 0 = désactivé
int module_anemo = 1; //1 = Anémomètre activé, 0 = désactivé
int module_girou = 1; //1 = Girouette activée, 0 = désactivée
int module_pluvio = 1; //1 = Pluviomètre activé, 0 = désactivé
int module_tension = 1; //1 = Mesure des tensions activée, 0 = désactivée
int module_bitvie = 1; //1 = Bitvie activé, 0 = désactivé



//Variable date heure
char datetime[20]; // taille suffisante pour "2025-08-25 21:45:59"

//old variable 
float old_vitesse = 0.0;

//Pins
#define BATTERY_PIN 34
#define SOLAR_PIN   35
constexpr uint8_t ANEMO_PIN = 18; // Pin de l'anémomètre
// GPIO (ESP32 WROOM32S) fournis par toi : 32,33,25,26,27,14,12,13
// Ordre : {N, NE, E, SE, S, SW, W, NW}
const uint8_t PINS_GIROUETTE[8] = { 32, 33, 25, 26, 27, 14, 12, 13 };
// invertLogic = true (par défaut) : actif quand LOW (reed->GND avec INPUT_PULLUP)
// usePullups = true (par défaut) : active INPUT_PULLUP
Girouette girouette(PINS_GIROUETTE, /*invertLogic*/ true, /*usePullups*/ true);
constexpr uint8_t PIN_BTN_CAL = 23;     // Bouton calibration Nord (vers GND)


// On garde une copie locale de l'offset Nord pour pouvoir recalculer un nouvel offset
// lors de la calibration (puisqu'on n'a pas de getter dans la classe).
float g_northOffsetDeg = 0.0f;

// ----------------- UTILS -----------------
static inline float wrap360f(float deg) {
  deg = fmodf(deg, 360.0f);
  if (deg < 0) deg += 360.0f;
  return deg;
}


// ---------- Gestion bouton calibration (appui long) ----------
bool btnPrev = HIGH;                // car INPUT_PULLUP
unsigned long btnDownAt = 0;
constexpr unsigned long LONG_PRESS_MS = 1500;

extern void setupLocalStationApiHandler(WebServer &server);
extern void maybeRefreshStationInfo();
extern bool fetchStationInfoFromRemote();
extern void startStationInfoBackgroundTask();

Modules mods{};      // état courant en mémoire
Modules lastMods{};  // pour comparaison

//Remise a l'heure du RTC avec la compilation de l'IDE

void setRTCFromNTP() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Erreur NTP");
        return;
    }

    // Ajuste le RTC avec l'heure locale (incluant heure d'été/hiver)
    rtc.adjust(DateTime(timeinfo.tm_year + 1900,
                        timeinfo.tm_mon + 1,
                        timeinfo.tm_mday,
                        timeinfo.tm_hour,
                        timeinfo.tm_min,
                        timeinfo.tm_sec));

    Serial.printf("RTC mis à jour : %02d/%02d/%04d %02d:%02d:%02d\n",
                  timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

void handleSetTime() {
    setRTCFromNTP();
    server.send(200, "text/plain", "RTC mis à l'heure !");
}

// Lit la config modules (id=1) depuis la table etatcapteurs via db_read.cpp
bool loadModulesFromDB(Modules& out) {
  // (facultatif mais recommandé si tu utilises db_mgr et un serveur async) :
  // DbLock _;

  int mb, md, ma, mg, mp, mt, mv;
  if (!readModulesById(1, mb, md, ma, mg, mp, mt, mv)) {
    return false;
  }
  out.bmp280 = mb;
  out.dht22  = md;
  out.anemo  = ma;
  out.girou  = mg;
  out.pluvio = mp;
  out.tension= mt;
  out.bitvie = mv;
  return true;
}


static bool modulesEqual(const Modules& a, const Modules& b) {
  return a.bmp280 == b.bmp280 &&
         a.dht22  == b.dht22  &&
         a.anemo  == b.anemo  &&
         a.girou  == b.girou  &&
         a.pluvio == b.pluvio &&
         a.tension== b.tension&&
         a.bitvie == b.bitvie;
}

// Déclaration de applyModuleChange pour éviter l'erreur de compilation
void applyModuleChange(const Modules& oldM, const Modules& newM) {
  if (oldM.anemo != newM.anemo) {
    if (newM.anemo) {
      anemo_init(ANEMO_PIN, 0.6667f, 0.0f, 1, 2000, 100.0f);
      pinMode(ANEMO_PIN, INPUT_PULLUP);
      Serial.println(F("[CFG] Anémomètre ACTIVÉ"));
    } else {
      Serial.println(F("[CFG] Anémomètre DÉSACTIVÉ"));
    }
  }
  // ... idem pour pluvio, bmp280, dht22, girouette, tension ...

  // Synchronise aussi tes variables globales existantes :
  module_bmp280 = newM.bmp280;
  module_dht22  = newM.dht22;
  module_anemo  = newM.anemo;
  module_girou  = newM.girou;
  module_pluvio = newM.pluvio;
  module_tension= newM.tension;
  module_bitvie = newM.bitvie;
}


void handleCalibrationButton() {
  bool s = digitalRead(PIN_BTN_CAL);
  unsigned long now = millis();

  // front descendant = appui
  if (btnPrev == HIGH && s == LOW) {
    btnDownAt = now;
  }
  // front montant = relâchement
  if (btnPrev == LOW && s == HIGH) {
    if (now - btnDownAt >= LONG_PRESS_MS) {
      // On lit un angle "actuel" (déjà avec l'offset en cours)
      // Fenêtre courte pour ne pas bloquer longtemps
      float angle = girouette.readAngle(24); // ~2x8ms d'attente interne (update 3 lectures)
      if (!isnan(angle)) {
        // Calibrer pour que l'angle courant devienne 0°
        // angle = wrap360(raw + g_northOffsetDeg)
        // => newOffset = wrap360(g_northOffsetDeg - angle)
        g_northOffsetDeg = wrap360f(g_northOffsetDeg - angle);
        girouette.setNorthOffsetDegrees(g_northOffsetDeg);
        Serial.printf("[Girouette] Calibration Nord OK ✅ (offset=%.1f°)\n", g_northOffsetDeg);
      } else {
        Serial.println("[Girouette] Calibration ignorée: angle invalide ❌");
      }
    } else {
      Serial.println("[Girouette] Appui court ignoré.");
    }
  }
  btnPrev = s;
}



void handleRoot() {
  File file = SPIFFS.open("/index.html", "r");
  if (file) {
    server.streamFile(file, "text/html");
    file.close();
  } else {
    server.send(404, "text/plain", "Fichier manquant");
  }
}


void handleData() {
  sqlite3_stmt *stmt;
  String json = "{";

  // Lecture de la dernière ligne de station_direct
  const char *sql = "SELECT tempdht22, humiditer, pression, tempbmp280, timestamp, tpsvie, pointderosee, anemometre, pluviometre, rafale FROM station_direct ORDER BY id DESC LIMIT 1;";
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      float temp = sqlite3_column_double(stmt, 0);
      float hum  = sqlite3_column_double(stmt, 1);
      float press = sqlite3_column_double(stmt, 2);
      float temp280 = sqlite3_column_double(stmt, 3);
      String ts = (const char *)sqlite3_column_text(stmt, 4);
      float tpsvie = sqlite3_column_double(stmt, 5);
      float pointderosee = sqlite3_column_double(stmt, 6);
      float anemometre = sqlite3_column_double(stmt, 7);
      old_vitesse = anemometre;
      float pluviometre = sqlite3_column_double(stmt, 8);
      float rafale = sqlite3_column_double(stmt, 9);

      json += "\"temperature\":" + String(temp) + ",";
      json += "\"humidite\":" + String(hum) + ",";
      json += "\"pression\":" + String(press) + ",";
      json += "\"tempbmp280\":" + String(temp280) + ",";
      json += "\"datetime\":\"" + ts + "\",";
      json += "\"tpsvie\":" + String(tpsvie) + ",";
      json += "\"pointderosee\":" + String(pointderosee) + ",";
      json += "\"anemometre\":" + String(anemometre) + ",";
      json += "\"pluviometre\":" + String(pluviometre) + ",";
      json += "\"rafale\":" + String(rafale) + ","; 
    }
    sqlite3_finalize(stmt);
  }

  // Lecture de la dernière ligne de tensions
  const char *sql2 = "SELECT tension_batterie, tension_solaire FROM tensions ORDER BY id DESC LIMIT 1;";
  if (sqlite3_prepare_v2(db, sql2, -1, &stmt, NULL) == SQLITE_OK) {
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      float tension_bat = sqlite3_column_double(stmt, 0);
      float tension_sol = sqlite3_column_double(stmt, 1);

      json += "\"tension_batterie\":" + String(tension_bat) + ",";
      json += "\"tension_solaire\":" + String(tension_sol);
    }
    sqlite3_finalize(stmt);
  }

  // Enlève la virgule finale si besoin (optionnel)
  if (json.endsWith(",")) json.remove(json.length() - 1);

  json += "}";

  server.send(200, "application/json", json);
}

/*
void connectWifiFromDB() {
  sqlite3_stmt *stmt;
  const char *sql = "SELECT ssid_wifi, pass_wifi FROM config ORDER BY id DESC LIMIT 1;";
  //WiFi.begin("Freebox-669838","burria52-ejectione-everberata!-vulnerate4");


  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      const char *ssid = (const char *)sqlite3_column_text(stmt, 0);
      const char *password = (const char *)sqlite3_column_text(stmt, 1);

      Serial.print("Connexion WiFi à : ");
      Serial.println(ssid);

      WiFi.begin(ssid, password);
      WiFi.setSleep(false);                 // latence et débit meilleurs
      // WiFi.setTxPower(WIFI_POWER_19_5dBm); // si couverture moyenne (optionnel)
      int retry = 0;
      while (WiFi.status() != WL_CONNECTED && retry < 20) {
        delay(500);
        Serial.print(".");
        retry++;
      }

      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ Connecté au WiFi !");
        Serial.println(WiFi.localIP());
      } else {
        Serial.println("\n❌ Impossible de se connecter au WiFi");
      }
    }
    sqlite3_finalize(stmt);
  }
}
*/



void connectWifiFromDB() {
  AppConfig cfg;
  if (!readAppConfig(cfg)) {
    Serial.println("❌ Lecture config Wi‑Fi échouée");
    return;
  }
  Serial.printf("Connexion Wi‑Fi à : %s\n", cfg.ssid_wifi.c_str());
  WiFi.begin(cfg.ssid_wifi.c_str(), cfg.pass_wifi.c_str());
  WiFi.setSleep(false);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Connecté au Wi‑Fi !");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ Impossible de se connecter au Wi‑Fi");
  }
}

#include <ArduinoJson.h>

void handleEtatCapteurs() {
  // DbLock _; // (facultatif, prépare Async)  // Supprimé car non défini
  StaticJsonDocument<512> doc;
  JsonObject etat = doc.createNestedObject("etat");
  JsonObject modules = doc.createNestedObject("modules");

  // etatcapteurs (etat)
  sqlite3_stmt* stmt=nullptr;
  const char* sql1 =
    "SELECT capteur_dht22, capteur_bmp280, capteur_pluvio, capteur_girou, "
    "capteur_anemo, tension_solaire, tension_batterie FROM etatcapteurs WHERE id=1;";
  if (sqlite3_prepare_v2(db, sql1, -1, &stmt, NULL) == SQLITE_OK &&
      sqlite3_step(stmt) == SQLITE_ROW) {
    etat["dht22"]            = sqlite3_column_int(stmt,0);
    etat["bmp280"]           = sqlite3_column_int(stmt,1);
    etat["pluvio"]           = sqlite3_column_int(stmt,2);
    etat["girou"]            = sqlite3_column_int(stmt,3);
    etat["anemo"]            = sqlite3_column_int(stmt,4);
    etat["tension_solaire"]  = sqlite3_column_double(stmt,5);
    etat["tension_batterie"] = sqlite3_column_double(stmt,6);
  }
  sqlite3_finalize(stmt);

  // etatcapteurs (modules)
  const char* sql2 =
    "SELECT module_bmp280, module_dht22, module_anemo, module_girou, "
    "module_pluvio, module_tension, module_bitvie FROM etatcapteurs WHERE id=1;";
  if (sqlite3_prepare_v2(db, sql2, -1, &stmt, NULL) == SQLITE_OK &&
      sqlite3_step(stmt) == SQLITE_ROW) {
    modules["bmp280"]  = sqlite3_column_int(stmt,0);
    modules["dht22"]   = sqlite3_column_int(stmt,1);
    modules["anemo"]   = sqlite3_column_int(stmt,2);
    modules["girou"]   = sqlite3_column_int(stmt,3);
    modules["pluvio"]  = sqlite3_column_int(stmt,4);
    modules["tension"] = sqlite3_column_int(stmt,5);
    modules["bitvie"]  = sqlite3_column_int(stmt,6);
  }
  sqlite3_finalize(stmt);

  String out; out.reserve(256);
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}



void setup() {
 
  Serial.begin(115200);
  
  
  // Mode AP pour configuration 
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("meteospit_Config", "12345678"); // SSID et mot de pas

  //Connection au wifi 
  if (!SPIFFS.begin(true)) {
    Serial.println("Erreur SPIFFS !");
    return;
  }

  
  if (!db_begin()) {
    Serial.println("❌ DB init KO");
  } else {
    Serial.println("✅ DB ouverte");
  }
  apiRefreshConfigFromDb();   // charge API_URL, API_KEY, STATION_ID, etc.
  /*
  int rc = sqlite3_open("/spiffs/station.db", &db);
  if (rc != SQLITE_OK) {
    Serial.print("Erreur ouverture base de données : ");
    Serial.println(sqlite3_errmsg(db));
  } else {
    Serial.println("Base de données ouverte avec succès !");
  }
  */
  Wire.begin();
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");
  if (!rtc.begin()) {
    Serial.println("RTC non détecté !");
    //while (1);
  }
  
  // Pour régler l'heure une fois :
  //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

    // Initialisation du capteur BMP280
  if (!initBMP280()) 
  {
      Serial.println(F("Failed to initialize BMP280. Check wiring."));
      bmp_ok = false;
    
  }else
  {
      bmp_ok = true;
      Serial.println(F("BMP280 initialized successfully."));
  }

  // Initialisation de l'anémomètre
  // pulsesPerRev = 1 par défaut (à ajuster si 2/4 aimants)
  anemo_init(ANEMO_PIN,
             0.6667f, // K m/s/Hz
             0.0f,    // C
             1,       // pulses per rev
             2000,    // période de mesure 2 s
             100.0f   // max Hz plausible
            );

  // Personnalisation des seuils d’état (optionnel)
  anemo_set_no_pulse_timeout_ms(10000); // 10 s
  anemo_set_stuck_timeout_ms(20000);    // 20 s



  //Declaration entree anemo
  pinMode(ANEMO_PIN, INPUT);   // interrupteur Reed à la pin 8
  // Configurer les pins ADC (au besoin, ajustez l'atténuation ici si nécessaire)
  analogSetPinAttenuation(BATTERY_PIN, ADC_11db);
  analogSetPinAttenuation(SOLAR_PIN, ADC_11db);

  //Initialisation du DHT22
  initDHT();

  // Initialisation du pluviomètre
  initPluviometre();

  // Initialisation de la girouette
  girouette.enableNorthButton(/*pin=*/23, /*activeLow=*/true, /*debounceMs=*/30, /*longPressMs=*/1500);
  girouette.begin();

  // Si ton "Nord" mécanique est décalé, règle un offset.
  // Ex: si ton "N" physique ferme la pin mappée "E" (90°), mets -90 pour que l'angle lise 0°.
  // girouette.setNorthOffsetDegrees(-90);
  
  
 // Bouton calibration
  pinMode(PIN_BTN_CAL, INPUT_PULLUP);

  // Girouette
  girouette.begin();
  girouette.setNorthOffsetDegrees(g_northOffsetDeg); // au cas où tu veux restaurer un offset sauvegardé

  Serial.println(F("[System] Init OK. Maintiens le bouton 1.5 s pour calibrer le Nord."));
     

  connectWifiFromDB(); 

// --- ROUTES HTTP (à mettre DANS setup(), après connectWifiFromDB(), avant server.begin()) ---
// Routes de base
server.on("/", handleRoot);
server.on("/data", handleData);
server.on("/settime", handleSetTime);
server.on("/etatcapteurs", HTTP_GET, handleEtatCapteurs);

// NotFound -> sert /images/... avec cache, sinon 404
server.onNotFound([]() {
  String uri = server.uri();
  if (uri.startsWith("/images/")) {
    sendWithCache(uri);
    return;
  }
  server.send(404, "text/plain", "Not found");
});

// /config
server.on("/config", HTTP_GET, []() {
  File file = SPIFFS.open("/config.html", "r");
  if (file) {
    server.streamFile(file, "text/html");
    file.close();
  } else {
    server.send(404, "text/plain", "Page config introuvable");
  }
});

// /style (MIME + cache)
server.on("/style", HTTP_GET, []() {
  const char* path = "/style.css";
  if (!SPIFFS.exists(path)) {
    server.send(404, "text/plain", "style.css introuvable");
    return;
  }
  File f = SPIFFS.open(path, "r");
  server.sendHeader("Cache-Control", "public, max-age=604800, immutable");
  server.streamFile(f, "text/css");
  f.close();
});

// /infos
server.on("/infos", HTTP_GET,  [] (){
  File file = SPIFFS.open("/infos.html", "r");
  if (file) {
    server.streamFile(file, "text/html");
    file.close();
  } else {
    server.send(404, "text/plain", "Page d'informations introuvable");
  }
});

// /update_config
server.on("/update_config", HTTP_POST, []() {
  int bmp280 = server.hasArg("bmp280") ? 1 : 0;
  int dht22  = server.hasArg("dht22")  ? 1 : 0;
  int anemo  = server.hasArg("anemo")  ? 1 : 0;
  int girou  = server.hasArg("girou")  ? 1 : 0;
  int pluvio = server.hasArg("pluvio") ? 1 : 0;
  int tension= server.hasArg("tension")? 1 : 0;
  int bitvie = server.hasArg("bitvie") ? 1 : 0;
  int api    = server.hasArg("api")    ? 1 : 0;

  updateModulesInDB(1, bmp280, dht22, anemo, girou, pluvio, tension, bitvie);
  updateActivationApiInDB(1, api);

  server.send(200, "text/html",
              "<h3>Configuration mise à jour !</h3><a href='/config'>Retour</a>");
});


// GET /config.json
server.on("/config.json", HTTP_GET, []() {
  AppConfig c;
  if (!readAppConfig(c)) {
    server.send(500, "application/json", "{\"error\":\"db read failed\"}");
    return;
  }
  StaticJsonDocument<512> doc;
  doc["ssid_wifi"]  = c.ssid_wifi;
  doc["pass_wifi"]  = String(c.pass_wifi.length(), '*'); // masque
  doc["IP_WIFI"]    = c.ip_wifi;
  doc["ID_STATION"] = c.id_station;
  doc["adresse_api"]= c.adresse_api;
  doc["token"]      = c.token;
  doc["activation_envoi_api"] = c.activation_envoi_api;
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
});

// POST /config.save
server.on("/config.save", HTTP_POST, []()  {
    AppConfig cur;
    readAppConfig(cur); // récupère l'ancien mdp si vide

    AppConfig n = cur;
    if (server.hasArg("ssid_wifi"))    n.ssid_wifi    = server.arg("ssid_wifi");
    if (server.hasArg("pass_wifi"))  { String p = server.arg("pass_wifi"); if (p != "") n.pass_wifi = p; }
    if (server.hasArg("IP_WIFI"))      n.ip_wifi      = server.arg("IP_WIFI");
    if (server.hasArg("ID_STATION"))   n.id_station   = server.arg("ID_STATION");
    if (server.hasArg("adresse_api"))  n.adresse_api  = server.arg("adresse_api");
    if (server.hasArg("token"))        n.token        = server.arg("token");
    if (server.hasArg("activation_envoi_api"))
                                       n.activation_envoi_api = server.arg("activation_envoi_api").toInt();

    if (!updateAppConfig(n)) {
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"db update failed\"}");
        return;
    }

    // Rafraîchit les variables runtime
    apiRefreshConfigFromDb();

    // Reconnexion Wi‑Fi si SSID/MdP ont changé
    if (n.ssid_wifi != cur.ssid_wifi || n.pass_wifi != cur.pass_wifi) {
        WiFi.disconnect(true, true);
        delay(300);
        WiFi.begin(n.ssid_wifi.c_str(), n.pass_wifi.c_str());
    }

    server.send(200, "application/json", "{\"ok\":true}");
});

// OTA
ElegantOTA.begin(&server);
server.begin();
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.println(file.name());
    file = root.openNextFile();
  }

  // dans setup(), après avoir initialisé `server` :
  setupLocalStationApiHandler(server);
  // fetch immédiat au démarrage
  startStationInfoBackgroundTask();
  

}



void loop() {
    static unsigned long previousTime = 0;  // Temps du dernier traitement
    unsigned long currentTime = millis();  // Temps actuel
    static unsigned long tDbHealth = 0;    // Ajout de la déclaration de tDbHealth
    //static unsigned long tPollMods = 0;    // Déclaration de tPollMods

    // Gestion des requêtes du serveur web
    server.handleClient();
    ElegantOTA.loop();
    

    if (millis() - tDbHealth >= 5000) {       // toutes les 5 s
      db_reopen_if_needed("/spiffs/station.db");                // réouvre seulement si nécessaire
      tDbHealth = millis();
    }

    
   // [CONSERVÉ] Poll modules (toutes 5 s) + applyModuleChange(...)

    static unsigned long tPollMods = 0;
    if (millis() - tPollMods >= 5000) {
      Modules newMods;
      if (loadModulesFromDB(newMods)) {
        if (!modulesEqual(newMods, mods)) {
          // Applique uniquement ce qui change
          applyModuleChange(mods, newMods);  // old, new
          mods = newMods;                    // mise à jour de l'état courant
          Serial.println(F("[CFG] Modules modifiés -> appliqués"));
        }
      } else {
        Serial.println(F("Erreur lors de la lecture des modules."));
      }
      tPollMods = millis();
    }


   
    //Gestion activation des modules
    if (updateModuleVariablesFromDB(1)) {
      Serial.println("Activation des modules mis à jour depuis la base !");
    } else {
      Serial.println("Erreur lors de la lecture des modules.");
    }

    if(module_anemo == 1) {
      etat_anemo = anemo_ok_bit_strict(); // 1 si OK, 0 sinon
          // Anémomètre
      anemo_update();
      // Mise à jour des données toutes les 2 secondes
      vitesse = anemo_get_speed_kmh();
      rafale = anemo_get_gust_kmh();
      Serial.print("Vitesse anémomètre: ");
      Serial.print(vitesse);
      Serial.println(" km/h");
    } else {
      etat_anemo = 0;
      vitesse = 0.0;
    }

    //envoi des données à l'API
    if(old_vitesse != vitesse){
      updateAnemometre(1, vitesse); // Met à jour la valeur de l'anémomètre dans la base de données
    }
    

    // Récupération des tensions panneau solaire + batterie
    tension_solaire = solaire();
    tension_batterie = batterie();

    //Pluviométre
    if (module_pluvio == 1)
    {
      gestionPluviometre();
      etat_pluvio = pluvio_active_bit();
      Serial.printf("etat_pluvio = %u\n", pluvio_active_bit());

      float pluie = obtenirQuantitePluie_mm();
      Serial.printf("Pluie cumulée : %.3f mm\n", pluie);
      quantite = pluie;
    }else
    {
      etat_pluvio = 0;
      quantite = 0.0;
    }
     
    DateTime now = rtc.now();
    sprintf(datetime, "%04d-%02d-%02d %02d:%02d:%02d",
          now.year(), now.month(), now.day(),
          now.hour(), now.minute(), now.second());

    Serial.print("DateTime = ");
    Serial.println(datetime);

    static unsigned long lastSendEtatStation = 0;
    if (millis() - lastSendEtatStation > 60000) { // toutes les 60 secondes
        sendLatestEtatStationMeteoToApi();
        lastSendEtatStation = millis();
    }
    // Effectuer les tâches toutes les 30 secondes (30000 ms)
    if (currentTime - previousTime >= 30000) {
        previousTime = currentTime;

      // Affichage des données dans le moniteur série
      Serial.println("--- Mise à jour des données ---");
      // BMP280
      if( module_bmp280 == 1) { 
        if (bmp_ok) {
          readBMP280(temp1, pression, altitude);
   
          if (!isnan(temp1) && !isnan(pression)) {
              etat_bmp280 = 1;
            } else {
              etat_bmp280 = 0;
            }
          } else {
            etat_bmp280 = 0;
          }
        } else {
          temp1 = 0.0;
          pression = 0.0;
          altitude = 0.0;
        }
        Serial.print("État BMP280 : ");
        Serial.println(etat_bmp280);
        //Au cas ou le BMP280 ne répond plus
        if (!bmp_ok) {
          bmp_ok = initBMP280();
        }

        Serial.print("Température BMP280: ");
        Serial.print(temp1);
        Serial.println(" °C");

        Serial.print("Pression: ");
        Serial.print(pression);
        Serial.println(" hPa");

        Serial.print("Altitude: ");
        Serial.print(altitude);
        Serial.println(" m");

        // DHT22
        temp2 = getTemperature();
        humiditer = getHumidity();
        if(module_dht22 == 1)
          {
          if(isDHTReady())
            {
                if (temp2 != -999.0 && humiditer != -999.0) {
                Serial.print("Température : ");
                Serial.print(temp2);
                Serial.print(" °C | Humidité : ");
                Serial.print(humiditer);
                Serial.println(" %");
                etat_dht22 = 1;
            } else {
                Serial.println("Erreur de lecture du DHT22.");
                etat_dht22 = 0;
            }

          }else
          {
            temp2 = 0.0;
            humiditer = 0.0;
          }
        } else
        {
            Serial.println("Capteur DHT22 non prêt.");
            etat_dht22 = 0;
        }
        Serial.print("État DHT22 : ");
        Serial.println(etat_dht22);

        // Calcul du point de rosée
        float point_de_rosee = calculPointRosee(temp1, humiditer);

        //Anemometre
        Serial.print("Anémomètre: ");
        Serial.print(anemo_get_speed_kmh(), 1);
        Serial.print(" km/h | Rafale=");
        Serial.print(anemo_get_gust_kmh(), 1);
        Serial.println(" km/h");
        Serial.print("État anémomètre : ");
        Serial.println(etat_anemo);


        Serial.print("Plui: ");
        Serial.print(quantite);
        Serial.println(" mm");

        Serial.print("Point de rosee: ");
        Serial.print(point_de_rosee);
        Serial.println(" °C");

        //Girouette
        if(module_girou == 1){
          // Lecture "simple"
        
          
          // 1) Mettre à jour la girouette avec un petit debounce non bloquant
          //    (update() lit 1 fois, puis 2 confirmations avec delay(debounceMs) → ici ~2x2ms)
          girouette.update(/*debounceMs=*/2, /*stableReads=*/3);

          
          // 2) Gérer le bouton (appui long -> calibration)
          handleCalibrationButton();

          // 3) Affichage périodique
          static unsigned long tPrint = 0;
          if (millis() - tPrint >= 1000) {
            tPrint = millis();

            // readAngle(windowMs) relance une petite fenêtre de mesure (~12-18ms selon windowMs)
            // Si tu veux éviter ce délai, on peut ajouter un getter dans la classe pour l'angle courant.
            degret = girouette.readAngle(30);
            const char* name = girouette.readName(30);

            Serial.print(F("[Girouette] Dir="));
            Serial.print(name);
            Serial.print(F(" | Angle="));
            if (isnan(degret)) Serial.println(F("NaN"));
            
            //float angle_etat = girouette.readAngle(30); // angle actuel
              if (!isnan(degret)) {
                etat_girou = 1; // fonctionne
              } else {
                etat_girou = 0; // problème
              }

              // Affichage pour debug
              Serial.print(F("Etat girouette: "));
              Serial.println(etat_girou);
            
          }
        }
        else
        {
          degret = 0;
          etat_girou = 0;
        }
      
        //Mise à jour de la base de données
        if (updateStationDirect(
            1,                // id de la ligne à mettre à jour
            temp2,            // tempdht22 (température DHT22)
            humiditer,        // humiditer
            temp1,            // tempbmp280 (température BMP280)
            pression,         // pression
            tension_solaire,  // lumiere (ou la variable correspondant à la luminosité)
            vitesse,          // anemometre
            degret,           // girouette (ou la variable correspondant à la direction)
            quantite,         // pluviometre
            point_de_rosee,   // pointderosee (à calculer si besoin)
            0.0,              // ghost (à définir selon ton usage)
            currentTime,               // tpsvie (à définir selon ton usage)
            datetime,
            rafale           // rafale (nouvelle variable pour la rafale)
        )) {
          Serial.println("Mise à jour réussie !");
        } else {
          Serial.println("Erreur lors de la mise à jour !");
        }

        if (updateTensions(
        1,                // id de la ligne à mettre à jour
        tension_batterie, // tension_batterie mesurée
        tension_solaire   // tension_solaire mesurée
        )) 
        {
            Serial.println("Tensions mises à jour !");
        } else {
            Serial.println("Erreur lors de la mise à jour des tensions !");
        }

        // Mise à jour de l'état des capteurs
        if(updateEtatCapteurs(
            1,                // id de la ligne à mettre à jour
            etat_dht22,
            etat_bmp280,
            etat_pluvio,
            etat_girou,
            etat_anemo,
            tension_batterie,
            tension_solaire
        )) 
        {
          Serial.println("État des capteurs mis à jour !");
        } else {
          Serial.println("Erreur lors de la mise à jour de l'état des capteurs !");
        }
        
        // Dans loop(), avant l'envoi à l'API :
        int activation = 0;
        if (readActivationApi(activation)) {
            activation_envoi_api = activation;
        } else {
            Serial.println("Erreur lecture activation_envoi_api !");
        }

        // Envoi des données à l'API si activé
        if (activation_envoi_api == 1) {
            sendLatestRowToApi();
        } else {
            Serial.println("Envoi des données à l'API désactivé.");
        }
      }
 

    delay(100);  // Réduit le blocage à 100 ms pour fluidifier les lectures
  }


