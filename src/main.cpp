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
#include "sht40.h"
#include "log.h"

//Stockage des données
#include <FS.h>
#include <LittleFS.h>

#include "esp_system.h"
#include "rom/rtc.h"



//Serveur web
#include <WiFi.h>
//#include "ElegantOTA.h" // commented out to avoid dual-OTA conflicts
#include <ArduinoOTA.h>
#include <WebServer.h>
//#include <ESPAsyncWebServer.h>

#include "freertos/task.h"

//Base de données
//#include "bd.h"
//#include "bd_mgr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

//RTC
#include <RTClib.h>

//Calculs
#include "pointderosee.h"

//Version
#include "version.h"

//Envoi des donnees 
#include "api.h"
//#include "db_read.h"
//#include "bd.h"



#include <cstring> // IMPORTANT pour memcmp


#include <Preferences.h>

Preferences prefs;

// Structure générique pour les min/max
struct StatScalar {
  float cur = NAN;      // valeur courante
  float minDay = NAN;   // min du jour
  float maxDay = NAN;   // max du jour
  float minAll = NAN;   // record min absolu
  float maxAll = NAN;   // record max absolu
  time_t tMinDay = 0, tMaxDay = 0;
  time_t tMinAll = 0, tMaxAll = 0;

  void update(float v, time_t now) {
    cur = v;
    if (isnan(v)) return;
    if (isnan(minDay) || v < minDay) { minDay = v; tMinDay = now; }
    if (isnan(maxDay) || v > maxDay) { maxDay = v; tMaxDay = now; }
    if (isnan(minAll) || v < minAll) { minAll = v; tMinAll = now; }
    if (isnan(maxAll) || v > maxAll) { maxAll = v; tMaxAll = now; }
  }

  void resetDaily() {
    minDay = NAN; maxDay = NAN;
    tMinDay = tMaxDay = 0;
  }
};

struct Modules {
  int bmp280;
  int dht22;
  int sht40;
  int anemo;
  int girou;
  int pluvio;
  int tension;
  int bitvie;
};

// Nos séries suivies
StatScalar ST_tempExt;    // basé sur temp1 (BMP280)
StatScalar ST_humExt;     // basé sur humiditer (DHT22)
StatScalar ST_press;      // basé sur pression (BMP280)
StatScalar ST_batt;       // basé sur tension_batterie
StatScalar ST_solar;      // basé sur tension_solaire

// Rafales
float GUST_maxDay = 0.0f;
float GUST_maxAll = 0.0f;
int   GUST_dirDay = -1;   // en degrés, au moment du max jour
int   GUST_dirAll = -1;   // en degrés, au moment du record absolu

// Pour limiter l'usure NVS, on sauvegardera seulement si un record change
static bool g_recordsDirty = false;
static unsigned long g_lastSaveMs = 0;


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
  if (!LittleFS.exists(path)) {
    server.send(404, "text/plain", "Not found");
    return;
  }
  File f = LittleFS.open(path, "r");
  server.sendHeader("Cache-Control", "public, max-age=2592000, immutable"); // ~30 jours
  server.streamFile(f, contentTypeFor(path));
  f.close();
}
void setRTCFromNTP();
//Variable fonctionement programme
// ===== Watchdog I²C : déclarations globales =====
#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN 21
#endif
#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN 22
#endif

static volatile bool g_i2cWdEnabled       = false;     // toggle runtime
static unsigned long g_lastI2CokMs        = 0;        // dernier succès I²C
static unsigned long g_lastI2CcheckMs     = 0;        // anti-spam
static int           g_i2cRecoverAttempts = 0;        // tentatives consécutives
static uint32_t      g_i2cWdReboots       = 0;        // compte reboots watchdog

static const unsigned long I2C_WATCHDOG_MS  = 5000UL;   // délai "pas de succès" avant recovery
static const int           I2C_MAX_RECOVER   = 3;        // au-delà -> reboot
static const unsigned long I2C_WD_GRACE_MS   = 30000UL;  // 30 s de grâce après boot

// Marquer un succès I²C (à appeler après lecture BMP/RTC valide)
static inline void noteI2C_OK() {
  g_lastI2CokMs = millis();
  g_i2cRecoverAttempts = 0;
}

// 9 clocks sur SCL + STOP pour libérer SDA si un esclave bloque la ligne
static void i2c_bus_clear() {
  Wire.end();
  pinMode(I2C_SCL_PIN, INPUT_PULLUP);
  pinMode(I2C_SDA_PIN, INPUT_PULLUP);
  delayMicroseconds(5);

  for (int i = 0; i < 9; ++i) {
    pinMode(I2C_SCL_PIN, OUTPUT);
    digitalWrite(I2C_SCL_PIN, LOW);
    delayMicroseconds(5);
    pinMode(I2C_SCL_PIN, INPUT); // remonte via pull-up
    delayMicroseconds(5);
  }

  // STOP : SDA low -> SCL high -> SDA high
  pinMode(I2C_SDA_PIN, OUTPUT);  digitalWrite(I2C_SDA_PIN, LOW);  delayMicroseconds(5);
  pinMode(I2C_SCL_PIN, INPUT);                                 delayMicroseconds(5);
  pinMode(I2C_SDA_PIN, INPUT);                                 delayMicroseconds(5);
}

// Séquence recovery : clear + Wire + soft reset BMP + RTC->NTP si invalide
static bool i2c_recover_sequence() {
  app_logf("[I2C] Recovery sequence: START");
  i2c_bus_clear();

  Wire.begin();
  delay(5);

  // Soft reset BMP280 + re-init
  #ifdef bmp_write8
  bmp_write8(0xE0, 0xB6);
  delay(10);
  #endif
  bool ok_bmp = initBMP280();
  app_logf("[I2C] BMP280 reinit -> %s", ok_bmp ? "OK" : "KO");

  // RTC : si invraisemblable, on remet à l'heure via NTP
  DateTime n = rtc.now();
  if (n.year() < 2024 || n.month() == 0 || n.day() == 0) {
    app_logf("[I2C] RTC invalide -> setRTCFromNTP()");
    setRTCFromNTP();
  }
  
  bool ok = ok_bmp; // critère minimal de succès
  app_logf("[I2C] Recovery sequence: %s", ok ? "OK" : "KO");
  return ok;
}

//Contrôle de l'alimentation 
//Declaration des variables globales pour la gestion de l'alimentation
// ---- Diagnostic alimentation : "Vcc 5V" proxy ----
// Utilise la tension solaire comme indicateur d'alim (ou batterie() si tu préfères)
float lireVCC5() {
  // si tu veux monitorer le panneau : 
  float v = solaire_cached();
  // ou la batterie :
  // float v = batterie_cached();
  if (isnan(v) || v <= 0.0f) return 0.0f;
  return v;
}
static uint32_t diag_reset_reason_core0 = 0;
static uint32_t diag_reset_reason_core1 = 0;
static float diag_vcc_min = 100.0f;
static float diag_vcc_max = 0.0f;
static uint32_t diag_brownout_count = 0;
static uint32_t diag_last_reboot_ms = 0;
static uint32_t diag_free_heap_boot = 0;
time_t g_boot_time = 0;  // Timestamp de démarrage en secondes (epoch) - GLOBAL

// prototype ADC interne 5V si tu mesures déjà le Vcc
extern float lireVCC5();  // OU tension_solaire si tu veux

// Compteur runtime (monotone), persistant via NVS
static uint32_t compteur = 0;

#include <Preferences.h>
static Preferences prefsCtr;

static void loadCounterFromNvs() {
  prefsCtr.begin("ctr", /*ro=*/true);
  compteur = prefsCtr.getULong("val", 0);
  prefsCtr.end();
}

static void saveCounterToNvs() {
  prefsCtr.begin("ctr", /*rw=*/false);
  prefsCtr.putULong("val", compteur);
  prefsCtr.end();
}

int activation_envoi_api = 1; //1 = envoi des données à l'API activé, 0 = désactivé

bool firstRun = true; // Pour envoi automatique au premier cycle

// --- Sanity-check BMP280 ---
static inline bool saneBMP(float tC, float p_hPa) {
  // Plages nominales BMP280 : T [-40; +85] °C, P [300; 1100] hPa
  return (!isnan(tC) && tC > -40.0f && tC < 85.0f) &&
         (!isnan(p_hPa) && p_hPa > 300.0f && p_hPa < 1100.0f);
}
static uint8_t bmpBadStreak = 0;  // compteur d’échecs consécutifs

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
int module_sht40 = 0; //1 = SHT40 activé, 0 = désactivé


String API_URL;
String API_URL_ETATSTATION;

// Flag global pour indiquer qu'une OTA est en cours (utilisé par api.cpp)
volatile bool otaInProgress = false;

static volatile bool bmpReinitReq = false;

// Variables de planification

static unsigned long nextDueRow  = 0;   // échéance stationdirect
static unsigned long nextDueEtat = 0;   // échéance etatstationmeteo
static bool pushBusy = false;           // vrai pendant un envoi HTTP


// Intervalles + jitter
constexpr unsigned long ROW_PERIOD_MS  = 30000;   // 30 s
constexpr unsigned long ETAT_PERIOD_MS = 60000;   // 60 s
constexpr unsigned long ROW_JITTER_MS  = 250;     // ~250 ms
constexpr unsigned long ETAT_OFFSET_MS = 5000;    // décalage 5 s


// Échéancier stable pour la mise à jour capteurs + DB (30 s)
static unsigned long nextUpdateMs = 0;
constexpr unsigned long UPDATE_PERIOD_MS = 30000;

// Helpers wrap-safe
static inline bool timeReached(unsigned long now, unsigned long dueAt) {
  return (long)(now - dueAt) >= 0; // wrap-safe
}

static inline void reschedStable(unsigned long &dueAt, unsigned long period) {
  dueAt += period; // période additionnelle -> régulier
  // Si on est déjà en retard (ex: bloc long), rattrape proprement
  while (timeReached(millis(), dueAt)) {
    dueAt += period;
  }
}

// Pousse dueAt par multiples de 'period' jusqu'à repasser dans le futur (wrap-safe)
static inline void reschedForwardToFuture(unsigned long &dueAt, unsigned long period) {
  unsigned long now = millis();
  while ((long)(now - dueAt) >= 0) {  // tant qu'échu ou en retard
    dueAt += period;                  // avance par pas stables
  }
}


// --- Offsets capteurs (appliqués aux valeurs lues) ---
struct Offsets {
  float t_bmp   = 0.0f;  // °C  (BMP280)
  float t_dht   = 0.0f;  // °C  (DHT22)
  float h_dht   = 0.0f;  // %   (DHT22)
  float press   = 0.0f;  // hPa (BMP280)
  float wind    = 0.0f;  // km/h (anémomètre: offset additif)
  float t_sht40 = 0.0f;  // °C  (SHT40 si utilisé)
};
static Offsets ofs;
static Preferences prefsOfs;  // NVS: namespace "ofs"

static void loadOffsetsFromNvs() {
  prefsOfs.begin("ofs", /*ro=*/true);
  ofs.t_bmp   = prefsOfs.getFloat("t_bmp",   0.0f);
  ofs.t_dht   = prefsOfs.getFloat("t_dht",   0.0f);
  ofs.h_dht   = prefsOfs.getFloat("h_dht",   0.0f);
  ofs.press   = prefsOfs.getFloat("press",   0.0f);
  ofs.wind    = prefsOfs.getFloat("wind",    0.0f);
  ofs.t_sht40 = prefsOfs.getFloat("t_sht40", 0.0f);
  prefsOfs.end();
}
static void saveOffsetsToNvs(const Offsets& o) {
  prefsOfs.begin("ofs", /*rw=*/false);
  prefsOfs.putFloat("t_bmp",   o.t_bmp);
  prefsOfs.putFloat("t_dht",   o.t_dht);
  prefsOfs.putFloat("h_dht",   o.h_dht);
  prefsOfs.putFloat("press",   o.press);
  prefsOfs.putFloat("wind",    o.wind);
  prefsOfs.putFloat("t_sht40", o.t_sht40);
  prefsOfs.end();
}


StationSnapshot g_snap;


// NVS pour modules + activation API
static Preferences prefsMods;

// Sauvegarde des modules + activation API (NVS)
static void saveModulesToNvs(const Modules& m, int api_active) {
  prefsMods.begin("modules", /*rw=*/false);
  prefsMods.putInt("bmp280", m.bmp280);
  prefsMods.putInt("dht22",  m.dht22);
  prefsMods.putInt("sht40",  m.sht40);
  prefsMods.putInt("anemo",  m.anemo);
  prefsMods.putInt("girou",  m.girou);
  prefsMods.putInt("pluvio", m.pluvio);
  prefsMods.putInt("tension",m.tension);
  prefsMods.putInt("bitvie", m.bitvie);
  prefsMods.putInt("api",    api_active);
  prefsMods.putUChar("ver",  1);  // petite “version” de schéma NVS
  prefsMods.end();
}

// Chargement des modules + activation API (NVS)
// Retourne true si données présentes, false sinon
static bool loadModulesFromNvs(Modules& m, int& api_active) {
  prefsMods.begin("modules", /*ro=*/true);
  bool ok = prefsMods.isKey("ver");
  m.bmp280 = prefsMods.getInt("bmp280", 1);
  m.dht22  = prefsMods.getInt("dht22",  1);
  m.sht40  = prefsMods.getInt("sht40",  0);
  m.anemo  = prefsMods.getInt("anemo",  1);
  m.girou  = prefsMods.getInt("girou",  1);
  m.pluvio = prefsMods.getInt("pluvio", 1);
  m.tension= prefsMods.getInt("tension",1);
  m.bitvie = prefsMods.getInt("bitvie", 1);
  api_active = prefsMods.getInt("api",  0);
  prefsMods.end();
  return ok;
}

//Variable date heure
char datetime[20]; // taille suffisante pour "2025-08-25 21:45:59"

//old variable 
float old_vitesse = 0.0;

//Pins
#define BATTERY_PIN 34
#define SOLAR_PIN   35

// --- Lecture ADC moyenne pour atténuer le bruit (ESP32 ADC) ---
static float readAdcAveraged(int pin, float fullScale = 3.6f, int N = 12) {
  uint32_t sum = 0;
  for (int i = 0; i < N; ++i) { sum += analogRead(pin); delay(2); }
  float raw = (float)sum / (float)N;
  return raw * fullScale / 4095.0f;  // 11 dB -> ~3.6 V
}

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


// --- Historique vent pour moyenne 10 min ---
static const uint16_t WIND_BUF_SECS = 600; // 10 minutes
static float g_windBuf[WIND_BUF_SECS];
static uint16_t g_windIdx = 0;
static bool g_windFilled = false;

// --- Snapshots de cumul pluie ---
static float g_rainCum_now = 0.0f;
static float g_rainCum_t0h = 0.0f;        // il y a 1 heure
static float g_rainCum_midnight = 0.0f;   // à 00:00 local du jour
static float g_rainCum_t0w = 0.0f;        // il y a 7 jours

// --- Horodatages pour savoir quand rafraîchir les snapshots ---
static unsigned long t_lastWindPush = 0;   // pour pousser 1 échantillon/s
static unsigned long t_lastHourMark = 0;
static uint8_t lastDaySeen = 255;          // pour détecter le passage à minuit
static uint16_t lastDoySeen = 65535;       // si tu veux aussi semaine glissante



// Nom cardinal simple (8 directions) depuis un angle 0..359, -1 => "—"
static const char* degToCardinal(int deg) {
  if (deg < 0) return "—";
  static const char* names[8] = {"N","NE","E","SE","S","SW","W","NW"};
  return names[((deg + 22) / 45) & 7];
}

static inline float windAvg10min() {
  uint16_t n = g_windFilled ? WIND_BUF_SECS : g_windIdx;
  if (n == 0) return 0.0f;
  double s = 0.0;
  for (uint16_t i = 0; i < n; ++i) s += g_windBuf[i];
  return (float)(s / (double)n);
}
static inline float rainHourMm()    { float d = g_rainCum_now - g_rainCum_t0h;       return d < 0 ? 0.0f : d; }
static inline float rainDayMm()     { float d = g_rainCum_now - g_rainCum_midnight;  return d < 0 ? 0.0f : d; }
static inline float rainWeekMm()    { float d = g_rainCum_now - g_rainCum_t0w;       return d < 0 ? 0.0f : d; }

unsigned long nowMsTest = 0;

// --- PROTOTYPES (à mettre AVANT handleData) ---
// Ne pas redonner de valeur par défaut ailleurs que ici
static void addNum(String& json, const char* key, float v, unsigned int decimals = 1);
static void addIntOrNull(String& json, const char* key, int v);

// --- DÉFINITIONS (au-dessus ou en-dessous de handleData, peu importe) ---
static void addNum(String& json, const char* key, float v, unsigned int decimals) {
  json += ",\""; json += key; json += "\":";
  if (isnan(v) || isinf(v)) {
    json += "null";
  } else {
    // Forcer la bonne surcharge de String: (double, unsigned int)
    json += String((double)v, (unsigned int)decimals);
  }
}

static void addIntOrNull(String& json, const char* key, int v) {
  json += ",\""; json += key; json += "\":";
  // On code -1 (inconnu) en null pour le front
  if (v < 0) json += "null";
  else       json += String(v);
}


// Arrondir à 2 décimales (préserver NaN)
static inline float round2f(float v) {
  if (isnan(v)) return v;
  return roundf(v * 100.0f) / 100.0f;
}

//Remise a l'heure du RTC avec la compilation de l'IDE

void setRTCFromNTP() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Erreur NTP");
        app_logf("Erreur NTP lors de la mise à l'heure du RTC");  
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




static bool modulesEqual(const Modules& a, const Modules& b) {
  return a.bmp280 == b.bmp280 &&
         a.dht22  == b.dht22  &&
         a.sht40  == b.sht40  &&
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
      Serial.println(F("[ANEMO] Anémomètre ACTIVÉ"));
      app_logf("[ANEMO] Anémomètre ACTIVÉ");
    } else {
      Serial.println(F("[ANEMO] Anémomètre DÉSACTIVÉ"));
      app_logf("[ANEMO] Anémomètre DÉSACTIVÉ");
    }
  }
  
  if (oldM.sht40 != newM.sht40) {
      if (newM.sht40) {
          initSHT40();
          Serial.println("[CFG] SHT40 ACTIVÉ");
          app_logf("[CFG] SHT40 ACTIVÉ");
      } else {
          Serial.println("[CFG] SHT40 DÉSACTIVÉ");
          app_logf("[CFG] SHT40 DÉSACTIVÉ");
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
  module_sht40 = newM.sht40;
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
        app_logf("[Girouette] Calibration Nord OK ✅ (offset=%.1f°)", g_northOffsetDeg);
      } else {
        Serial.println("[Girouette] Calibration ignorée: angle invalide ❌");
        app_logf("[Girouette] Calibration ignorée: angle invalide ❌");
      }
    } else {
      Serial.println("[Girouette] Appui court ignoré.");
      app_logf("[Girouette] Appui court ignoré.");
    }
  }
  btnPrev = s;
}


void handleRoot() {
  File file = LittleFS.open("/index.html", "r");
  if (file) {
    server.streamFile(file, "text/html");
    file.close();
  } else {
    server.send(404, "text/plain", "Fichier manquant");
  }
}


// Conversion RSSI -> pourcentage (approximation douce)
static int rssiToPercent(int rssi) {
  // -100 dBm => 0%,  -50 dBm => 100%
  int pct = (rssi <= -100) ? 0 : (rssi >= -50 ? 100 : 2 * (rssi + 100));
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  return pct;
}
static int pctToBars(int pct) {
  if (pct >= 80) return 4;
  if (pct >= 55) return 3;
  if (pct >= 30) return 2;
  if (pct >  0)  return 1;
  return 0;
}



// Accès DB partagés (définis dans bd_mgr.cpp)
//extern sqlite3* db;
//extern SemaphoreHandle_t g_dbMutex;

/*
// Petites fonctions d’aide pour le mutex
static inline bool dbLock(uint32_t ms = 300) {
  return g_dbMutex && xSemaphoreTake(g_dbMutex, pdMS_TO_TICKS(ms)) == pdTRUE;
}
static inline void dbUnlock() {
  if (g_dbMutex) xSemaphoreGive(g_dbMutex);
}
*/
// --- Helpers JSON sûrs ---
// Retourne "null" si v est NaN/Inf, sinon le nombre formaté avec 'dec' décimales.
static inline String jsonNumberOrNull(float v, int dec = 2) {
  if (isnan(v) || isinf(v)) return "null";
  return String(v, dec);
}

// Retourne "null" si s est vide / invalide, sinon la chaîne JSON-quotée.
static inline String jsonStringOrNull(const char* s) {
  if (!s || !*s) return "null";
  // Ici g_snap.datetime est déjà ASCII "YYYY-MM-DD HH:MM:SS" donc pas besoin d'escape
  return String("\"") + s + "\"";
}


void handleData() {
  if (otaInProgress) { server.send(503, "application/json", "{\"error\":\"OTA in progress\"}"); return; }

  String json; json.reserve(2048);
  json = "{";

  // 1) Mesures courantes (depuis le snapshot RAM)
  json += "\"temperature\":";   json += jsonNumberOrNull(g_snap.temp_dht, 2);
  json += ",\"humidite\":";     json += jsonNumberOrNull(g_snap.hum, 2);

  // BMP280 (déjà clampé côté snapshot : on sécurise encore la sérialisation)
  json += ",\"tempbmp280\":";   json += jsonNumberOrNull(g_snap.temp_bmp, 2);
  json += ",\"pression\":";     json += jsonNumberOrNull(g_snap.press_hPa, 2);

  // Datetime : null si vide
  json += ",\"datetime\":";     json += jsonStringOrNull(g_snap.datetime);

  json += ",\"tpsvie\":";       json += String((unsigned long)(millis() / 1000UL));
  json += ",\"pointderosee\":"; json += jsonNumberOrNull(g_snap.dew, 2);

  json += ",\"anemometre\":";   json += jsonNumberOrNull(g_snap.wind, 2);
  json += ",\"pluviometre\":";  json += jsonNumberOrNull(g_snap.rain_cum, 2);
  json += ",\"rafale\":";       json += jsonNumberOrNull(g_snap.gust, 2);

  // 2) KPI calculés côté RAM
  json += ",\"moyenne10min\":"; json += jsonNumberOrNull(windAvg10min(), 2);
  json += ",\"direction\":";    json += String(max(0, g_snap.dir_deg));  // int sûr
  json += ",\"pluieHeure\":";   json += jsonNumberOrNull(rainHourMm(), 2);
  json += ",\"pluieJour\":";    json += jsonNumberOrNull(rainDayMm(), 2);
  json += ",\"pluieSemaine\":"; json += jsonNumberOrNull(rainWeekMm(), 2);

  // 3) Wi‑Fi
  int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -100;
  int pct  = rssiToPercent(rssi);
  int bars = pctToBars(pct);
  json += ",\"wifi_rssi\":";    json += String(rssi);
  json += ",\"wifi_percent\":"; json += String(pct);
  json += ",\"wifi_bars\":";    json += String(bars);
  json += ",\"compteur\":";     json += String((unsigned long)compteur);

  // 4) État BMP pour l’UI
  json += ",\"bmp_status\":";       json += String((int)etat_bmp280);
  json += ",\"bmp_bad_streak\":";   json += String((unsigned)bmpBadStreak);

  // 5) Min/Max jour + All‑time (tes helpers addNum/addIntOrNull gèrent déjà NaN -> null)
  addNum(json, "t_min_day", ST_tempExt.minDay, 1);
  addNum(json, "t_max_day", ST_tempExt.maxDay, 1);
  addNum(json, "t_min_all", ST_tempExt.minAll, 1);
  addNum(json, "t_max_all", ST_tempExt.maxAll, 1);

  addNum(json, "h_min_day", ST_humExt.minDay, 0);
  addNum(json, "h_max_day", ST_humExt.maxDay, 0);
  addNum(json, "h_min_all", ST_humExt.minAll, 0);
  addNum(json, "h_max_all", ST_humExt.maxAll, 0);

  addNum(json, "p_min_day", ST_press.minDay, 1);
  addNum(json, "p_max_day", ST_press.maxDay, 1);
  addNum(json, "p_min_all", ST_press.minAll, 1);
  addNum(json, "p_max_all", ST_press.maxAll, 1);

  addNum(json, "vb_min_day", ST_batt.minDay, 2);
  addNum(json, "vb_max_day", ST_batt.maxDay, 2);
  addNum(json, "vb_min_all", ST_batt.minAll, 2);
  addNum(json, "vb_max_all", ST_batt.maxAll, 2);

  addNum(json, "vs_min_day", ST_solar.minDay, 2);
  addNum(json, "vs_max_day", ST_solar.maxDay, 2);
  addNum(json, "vs_min_all", ST_solar.minAll, 2);
  addNum(json, "vs_max_all", ST_solar.maxAll, 2);

  // 6) Rafales + directions
  addNum(json, "gust_max_day", GUST_maxDay, 1);
  addIntOrNull(json, "gust_dir_day", GUST_dirDay);
  addNum(json, "gust_max_all", GUST_maxAll, 1);
  addIntOrNull(json, "gust_dir_all", GUST_dirAll);

  json += "}";
  server.send(200, "application/json", json);

  // DEBUG (temporaire) : décommente pour voir la chaîne brute
  // app_logf("[/data] %s", json.c_str());
}



// --- Gestion Wi‑Fi STA + AP sécurisé WPA2 ---

// Etat STA non-bloquant
static bool g_staConnected = false;
static unsigned long g_nextReconnectMs = 0;
static uint32_t g_backoffMs = 2000;   // 2s -> 4s -> 8s ... max 60s

// NVS pour authentification & AP
static Preferences prefsAuth; // namespace "auth"
static Preferences prefsAp;   // namespace "ap"

// Config Auth/AP en RAM
struct AuthConfig {
  String viewerUser = "viewer";
  String viewerPass = "";   // généré au premier boot si vide
  String adminUser  = "admin";
  String adminPass  = "";   // généré au premier boot si vide
};

struct ApConfig {
  String ssid = "METEOSPIT";
  String pass = "";         // généré au premier boot si vide (>=12 chars)
  uint8_t channel = 1;
  uint8_t maxConn = 2;      // écran + smartphone
};

static AuthConfig g_auth;
static ApConfig   g_ap;

// Petit utilitaire de génération aléatoire (A..Z a..z 0..9)
static String makeRandomString(size_t len) {
  const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789";
  size_t n = sizeof(alphabet) - 1;
  String s; s.reserve(len);
  for (size_t i=0; i<len; ++i) {
    uint32_t r = esp_random();
    s += alphabet[r % n];
  }
  return s;
}

// Chargement NVS Auth


static void loadAuthFromNvs() {
  prefsAuth.begin("auth", /*ro=*/true);
  g_auth.viewerUser = prefsAuth.getString("viewer_user", "viewer");
  g_auth.viewerPass = prefsAuth.getString("viewer_pass", "");    // si vide -> on mettra défaut
  g_auth.adminUser  = prefsAuth.getString("admin_user",  "admin");
  g_auth.adminPass  = prefsAuth.getString("admin_pass",  "");
  prefsAuth.end();

  // ➜ Normalisation : si vide ou trop court, on prend les valeurs par défaut Option A
  if (g_auth.viewerUser.isEmpty()) g_auth.viewerUser = "viewer";
  if (g_auth.viewerPass.length() < 4) g_auth.viewerPass = "meteoviewer";
  if (g_auth.adminUser.isEmpty())  g_auth.adminUser  = "admin";
  if (g_auth.adminPass.length()  < 6) g_auth.adminPass  = "meteoadmin";

  // ⬇️ Log d’info (masqué) pour vérifier rapidement sur le port série

  app_logf("[Auth] viewer=%s / %u chars, admin=%s / %u chars",
    g_auth.viewerUser.c_str(), (unsigned)g_auth.viewerPass.length(),
    g_auth.adminUser.c_str(),  (unsigned)g_auth.adminPass.length());

}

static void loadApFromNvs() {
  prefsAp.begin("ap", /*ro=*/true);
  g_ap.ssid    = prefsAp.getString("ssid", "METEOSPIT");
  g_ap.pass    = prefsAp.getString("pass", "");    // si vide -> défaut
  g_ap.channel = prefsAp.getUChar("chan", 1);
  g_ap.maxConn = prefsAp.getUChar("maxc", 2);
  prefsAp.end();

  // ➜ Normalisation : si mot de passe AP trop court, on met celui de l’option A
  if (g_ap.ssid.isEmpty()) g_ap.ssid = "METEOSPIT";
  if (g_ap.pass.length() < 8) g_ap.pass = "meteospit-setup";
  if (g_ap.channel < 1 || g_ap.channel > 13) g_ap.channel = 1;
  if (g_ap.maxConn == 0 || g_ap.maxConn > 4) g_ap.maxConn = 2;

  Serial.printf("[AP] SSID='%s' pass=%u chars, ch=%u, max=%u\n",
    g_ap.ssid.c_str(), (unsigned)g_ap.pass.length(), g_ap.channel, g_ap.maxConn);
}

// Démarrage AP local sécurisé WPA2, IP fixe 192.168.4.1
static void startLocalAP() {
  IPAddress apIP(192,168,4,1), apGW(192,168,4,1), apMask(255,255,255,0);
  WiFi.softAPConfig(apIP, apGW, apMask);
  WiFi.softAP(g_ap.ssid.c_str(), g_ap.pass.c_str(), g_ap.channel, /*hidden=*/false, g_ap.maxConn);
  app_logf("[AP] ssid=%s pass_len=%u, ch=%u, max=%u",
    g_ap.ssid.c_str(), (unsigned)g_ap.pass.length(), g_ap.channel, g_ap.maxConn);

}

// Gestionnaire d’événements Wi‑Fi STA (non bloquant)
static void onWiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      g_staConnected = true;
      g_backoffMs = 2000;
      Serial.printf("[WiFi] STA GOT IP: %s (ch=%d)\n",
                    WiFi.localIP().toString().c_str(), WiFi.channel());
      if (firstRun) {
        nextUpdateMs = millis(); // forcer une mise à jour rapide après connexion
        Serial.println("WiFi connecté : envoi initial programmé");
      }
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      g_staConnected = false;
      Serial.println("[WiFi] STA DISCONNECTED");
      g_nextReconnectMs = millis() + g_backoffMs;
      g_backoffMs = std::min<uint32_t>(g_backoffMs * 2, 60000);
      break;
    default: break;
  }
}

// Lancer la connexion STA depuis NVS (sans bloquer)
static void startStaFromNVS() {

  Preferences prefs;
  prefs.begin("wifi", false);

  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");

  prefs.end();

  if (ssid == "") {
    Serial.println("❌ Aucun WiFi configuré (NVS)");
    return;
  }

  WiFi.begin(ssid.c_str(), pass.c_str());

  Serial.printf("[WiFi] STA connect à SSID '%s' (non bloquant)\n", ssid.c_str());
}

// Helpers Auth HTTP
static bool requireViewerAuth() {
  if (!server.authenticate(g_auth.viewerUser.c_str(), g_auth.viewerPass.c_str())) {
    server.requestAuthentication(); // 401
    return false;
  }
  return true;
}
static bool requireAdminAuth() {
  if (!server.authenticate(g_auth.adminUser.c_str(), g_auth.adminPass.c_str())) {
    server.requestAuthentication();
    return false;
  }
  return true;
}



void connectWifiFromNVS() {

  Preferences prefs;
  prefs.begin("wifi", false); // lecture seule

  String ssid = prefs.getString("ssid", "Freebox-669838");
  String pass = prefs.getString("pass", "burria52-ejectione-everberata!-vulnerate4");

  prefs.end();

  if (ssid == "") {
    Serial.println("❌ Aucun WiFi configuré");
    return;
  }

  Serial.printf("Connexion WiFi : %s\n", ssid.c_str());

  WiFi.begin(ssid.c_str(), pass.c_str());
  WiFi.setSleep(false);

  int retry = 0;

  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Connecté !");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ Connexion échouée");
  }
}


void handleEtatCapteurs() {
  StaticJsonDocument<512> doc;

  // États capteurs en RAM (tes flags et valeurs courantes)
  JsonObject etat = doc.createNestedObject("etat");
  etat["dht22"]            = etat_dht22;
  etat["bmp280"]           = etat_bmp280;
  etat["pluvio"]           = etat_pluvio;
  etat["girou"]            = etat_girou;
  etat["anemo"]            = etat_anemo;
  etat["tension_solaire"]  = g_snap.solar_v;  // <-- RAM, plus DB
  etat["tension_batterie"] = g_snap.batt_v;   // <-- RAM, plus DB

  // Modules (source: runtime/NVS)
  JsonObject modules = doc.createNestedObject("modules");
  modules["bmp280"]  = module_bmp280;
  modules["dht22"]   = module_dht22;
  modules["sht40"]   = module_sht40;
  modules["anemo"]   = module_anemo;
  modules["girou"]   = module_girou;
  modules["pluvio"]  = module_pluvio;
  modules["tension"] = module_tension;
  modules["bitvie"]  = module_bitvie;
  modules["api"]     = activation_envoi_api;  // 1/0

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// --- NTP/UI (en mémoire, tu peux persister plus tard si tu veux) ---
static String g_ntp_server = "pool.ntp.org";
static String g_timezone   = "Europe/Paris"; // ta règle tz CEST est déjà configurée plus haut


// NVS pour la conf "app"
static Preferences prefsCfg;

// Conversion "simple" IANA -> POSIX (complète ce map si tu veux d'autres zones)
static String ianaToPosix(const String& tz) {
  // Quelques exemples les plus utiles
  if (tz.equalsIgnoreCase("Europe/Paris"))    return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz.equalsIgnoreCase("Europe/Berlin"))   return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz.equalsIgnoreCase("Europe/Madrid"))   return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz.equalsIgnoreCase("UTC") || tz.equalsIgnoreCase("Etc/UTC")) return "UTC0";

  // Si l'utilisateur fournit déjà une chaîne POSIX (contient "CEST" "CET" etc.), on ne change pas
  if (tz.indexOf("CEST") >= 0 || tz.indexOf("CET") >= 0 || tz.indexOf("UTC") >= 0) return tz;

  // Par défaut, retourne tel quel (au cas où l’ESP saurait l’interpréter)
  return tz;
}

static void applyTimeConfig(const String& ianaOrPosixTz, const String& ntp) {
  String tzPosix = ianaToPosix(ianaOrPosixTz);
  configTzTime(tzPosix.c_str(), ntp.c_str());  // applique tz+NTP au système
  Serial.printf("[Time] TZ='%s' (POSIX='%s') NTP='%s'\n",
                ianaOrPosixTz.c_str(), tzPosix.c_str(), ntp.c_str());
}

static void loadNtpTzFromNvs() {
  prefsCfg.begin("appcfg", /*ro=*/true);
  String ntp = prefsCfg.getString("ntp_server", g_ntp_server);
  String tz  = prefsCfg.getString("timezone",   g_timezone);
  prefsCfg.end();
  g_ntp_server = ntp;
  g_timezone   = tz;
}

static void saveNtpTzToNvs() {
  prefsCfg.begin("appcfg", /*rw=*/false);
  prefsCfg.putString("ntp_server", g_ntp_server);
  prefsCfg.putString("timezone",   g_timezone);
  prefsCfg.end();
}


void handleStatusJson() {

  // 🔹 Lecture WiFi depuis NVS
  Preferences prefsWifi;
  prefsWifi.begin("wifi", true);

  String ssid = prefsWifi.getString("ssid", "");
  prefsWifi.end();

  // 🔹 Lecture API depuis NVS
  Preferences prefsApi;
  prefsApi.begin("api", true);

  String api_url = prefsApi.getString("base", "");
  String token   = prefsApi.getString("key", "");
  int stationId  = prefsApi.getInt("id", 1);

  prefsApi.end();

  StaticJsonDocument<1024> j;

  // 1) Modules
  JsonObject mods = j.createNestedObject("modules");
  mods["bmp280"] = module_bmp280;
  mods["dht22"]  = module_dht22;
  mods["sht40"]  = module_sht40;
  mods["anemo"]  = module_anemo;
  mods["vane"]   = module_girou;
  mods["rain"]   = module_pluvio;
  mods["bitvie"] = module_bitvie;

  // 2) États
  JsonObject st = j.createNestedObject("state");
  st["bmp280"] = etat_bmp280;
  st["dht22"]  = etat_dht22;
  st["sht40"]  = (module_sht40 == 1);
  st["anemo"]  = etat_anemo;
  st["vane"]   = etat_girou;
  st["rain"]   = etat_pluvio;

  // 3) Valeurs
  JsonObject jb = j.createNestedObject("bmp280");
  jb["temp"]  = g_snap.temp_bmp;
  jb["press"] = g_snap.press_hPa;

  JsonObject jd = j.createNestedObject("dht22");
  jd["temp"] = g_snap.temp_dht;
  jd["hum"]  = g_snap.hum;

  JsonObject ja = j.createNestedObject("anemo");
  ja["speed"] = g_snap.wind;
  ja["gust"]  = g_snap.gust;

  JsonObject jv = j.createNestedObject("vane");
  jv["deg"]  = degret;
  jv["card"] = "—";

  JsonObject jr = j.createNestedObject("rain");
  jr["mm"] = quantite;

  JsonObject jvolt = j.createNestedObject("volt");
  jvolt["solar"] = tension_solaire;
  jvolt["batt"]  = tension_batterie;

  // 4) Config
  JsonObject jc = j.createNestedObject("conf");
  jc["bmp280_addr"] = String("0x") + String(g_bmp_addr, HEX);
  jc["api_url"]     = api_url;
  jc["api_token"]   = token;
  jc["api_enabled"] = (activation_envoi_api != 0);
  jc["ntp_server"]  = g_ntp_server;
  jc["timezone"]    = g_timezone;

  jc["i2c_wd_enabled"]   = g_i2cWdEnabled;
  jc["i2c_last_ok_ms"]   = (uint32_t)g_lastI2CokMs;
  jc["i2c_recover_count"]= g_i2cRecoverAttempts;
  jc["i2c_wd_reboots"]   = (uint32_t)g_i2cWdReboots;
  jc["i2c_state"]        = ((millis() - g_lastI2CokMs) > I2C_WATCHDOG_MS ? "warning" : "ok");

  // 5) Réseau
  JsonObject jn = j.createNestedObject("net");
  jn["ssid"]          = ssid;
  jn["wifi_password"] = "";
  jn["ip_wifi"]       = "dhcp";
  jn["id_station"]    = stationId;

  // 6) KPI
  JsonObject ui = j.createNestedObject("ui");
  ui["wind_avg10"] = windAvg10min();
  ui["rain_hour"]  = rainHourMm();
  ui["rain_day"]   = rainDayMm();
  ui["rain_week"]  = rainWeekMm();
  ui["dir_deg"]    = degret;
  ui["dir_card"]   = "—";

  int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -100;
  int pct  = rssiToPercent(rssi);
  int bars = pctToBars(pct);

  ui["wifi_rssi"]   = rssi;
  ui["wifi_percent"]= pct;
  ui["wifi_bars"]   = bars;

  String out;
  serializeJson(j, out);

  server.send(200, "application/json", out);
}


static float readP(const char* key, float def = NAN) {
  return prefs.getFloat(key, def);
}
static void writeP(const char* key, float v) {
  prefs.putFloat(key, v);
}


void resetDailyStatsAtMidnightIfNeeded(const DateTime& now) {
  // Tu utilises déjà lastDaySeen pour la pluie -> on s'appuie dessus
  if (lastDaySeen != now.day()) {
    lastDaySeen = now.day();

    ST_tempExt.resetDaily();
    ST_humExt.resetDaily();
    ST_press.resetDaily();
    ST_batt.resetDaily();
    ST_solar.resetDaily();

    GUST_maxDay = 0.0f;
    GUST_dirDay = -1;

    Serial.println("[Daily] Reset des min/max du jour à minuit.");
  }
}

// --- Charger les records “all-time” au boot ---
void loadAllTimeRecords() {
  // Température (BMP/DHT, selon ce que tu alimentes)
  ST_tempExt.minAll = readP("t_min_all");   // NAN si jamais écrit
  ST_tempExt.maxAll = readP("t_max_all");

  // Humidité
  ST_humExt.minAll  = readP("h_min_all");
  ST_humExt.maxAll  = readP("h_max_all");

  // Pression
  ST_press.minAll   = readP("p_min_all");
  ST_press.maxAll   = readP("p_max_all");

  // Batterie / Solaire
  ST_batt.minAll    = readP("vb_min_all");
  ST_batt.maxAll    = readP("vb_max_all");
  ST_solar.minAll   = readP("vs_min_all");
  ST_solar.maxAll   = readP("vs_max_all");

  // Rafale + direction associée (stockée en float pour simplifier)
  GUST_maxAll       = readP("gust_max_all", 0.0f);
  float gustDirAllF = readP("gust_dir_all", NAN);
  GUST_dirAll       = isnan(gustDirAllF) ? -1 : (int)gustDirAllF;

  Serial.println("[Records] All-time chargés depuis NVS.");
}

// --- Sauvegarde anti-usure si des records ont vraiment changé ---
void saveAllTimeRecordsIfDirty() {
  const unsigned long SAVE_PERIOD_MS = 60000;  // pas plus d'1 écriture/min
  unsigned long nowMs = millis();
  if (!g_recordsDirty) return;
  if (nowMs - g_lastSaveMs < SAVE_PERIOD_MS) return;

  // Température
  writeP("t_min_all", ST_tempExt.minAll);
  writeP("t_max_all", ST_tempExt.maxAll);

  // Humidité
  writeP("h_min_all", ST_humExt.minAll);
  writeP("h_max_all", ST_humExt.maxAll);

  // Pression
  writeP("p_min_all", ST_press.minAll);
  writeP("p_max_all", ST_press.maxAll);

  // Batterie / solaire
  writeP("vb_min_all", ST_batt.minAll);
  writeP("vb_max_all", ST_batt.maxAll);
  writeP("vs_min_all", ST_solar.minAll);
  writeP("vs_max_all", ST_solar.maxAll);

  // Rafale
  writeP("gust_max_all", GUST_maxAll);
  writeP("gust_dir_all", (float)GUST_dirAll); // direction stockée en float

  g_lastSaveMs = nowMs;
  g_recordsDirty = false;
  Serial.println("[Records] All-time sauvegardés en NVS.");
}


// --- Nouvelle fonction utilitaire ---
// Met à jour la stat et marque 'dirty' si min/max ALL-TIME ont changé
inline void updateStat(StatScalar& s, float v, time_t now) {
  float prevMinAll = s.minAll;
  float prevMaxAll = s.maxAll;
  s.update(v, now);
  if ((prevMinAll != s.minAll) || (prevMaxAll != s.maxAll)) {
    g_recordsDirty = true;
  }
}



// Entêtes anti-cache pour les réponses HTTP
static inline void sendNoCacheHeaders(WebServer& srv) {
  srv.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  srv.sendHeader("Pragma", "no-cache");
}




// Réinitialisation NVS Auth/AP aux valeurs par défaut (pour debug)
//static void resetAuthApToDefaults() {
//  prefsAuth.begin("auth", /*rw=*/false); prefsAuth.clear(); prefsAuth.end();
//  prefsAp.begin("ap",     /*rw=*/false); prefsAp.clear();   prefsAp.end();
//  Serial.println("[Auth/AP] NVS cleared. Reboot to reapply defaults.");
//}
void debugListFiles() {
    Serial.println("---- LISTE LITTLEFS ----");

    File root = LittleFS.open("/");
    if (!root) {
        Serial.println("Erreur ouverture racine !");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        Serial.print("Fichier: ");
        Serial.print(file.name());
        Serial.print(" | Taille: ");
        Serial.println(file.size());
        file = root.openNextFile();
    }

    Serial.println("------------------------");
}



void setup() {
 
  Serial.begin(115200);


  RESET_REASON r0 = rtc_get_reset_reason(0);
  RESET_REASON r1 = rtc_get_reset_reason(1);
  diag_reset_reason_core0 = r0;
  diag_reset_reason_core1 = r1;
  diag_free_heap_boot = ESP.getFreeHeap();
  diag_last_reboot_ms = millis();

  app_logf("[BOOT] Reset reason core0=%d core1=%d", r0, r1);
  app_logf("[BOOT] Free heap at boot: %u", diag_free_heap_boot);
  
  //resetAuthApToDefaults();

  //Connection au wifi 

  
  Preferences prefs;
  prefs.begin("wifi", false);

  prefs.putString("ssid", "Freebox-669838");
  prefs.putString("pass", "burria52-ejectione-everberata!-vulnerate4");

  prefs.end();

  //Enregistrement activation envoi API dans NVS
  Preferences prefsMods;
  prefsMods.begin("modules", false);
  prefsMods.putInt("api", activation_envoi_api);
  prefsMods.end();

  Serial.printf("API activation = %d\n", activation_envoi_api);

  connectWifiFromNVS();

  if (!LittleFS.begin(true)) {
    Serial.println("Erreur LittleFS !");
    return;
  }

  debugListFiles();

  
  log_init("/log.txt", 100);               // fichier + limite
  log_set_flush_period(60000);             // 60 s
  app_logf("Boot station");        
  // Charger le compteur persistant
  loadCounterFromNvs();
  Serial.printf("[CTR] compteur (boot) = %lu\n", (unsigned long)compteur);

  // première ligne

  unsigned long nowMs = millis();
  // ⚡ Première mise à jour immédiatement au démarrage
  nextUpdateMs = nowMs;

  // Envoi stationdirect 5 s après la mise à jour DB
  nextDueRow  = nextUpdateMs + 5000;            // ≈ T+5 s

  // Envoi etatstationmeteo ≈ 60 s après la mise à jour DB
  nextDueEtat = nextUpdateMs + 65000;           // ≈ T+67 s

  // Jitter optionnel
  randomSeed((uint32_t)esp_random());
  nextDueRow  += random(100, 400);
  nextDueEtat += random(100, 400);

  
  //nextUpdateMs = millis() + UPDATE_PERIOD_MS;     // 1re mise à jour dans 30 s
  // --- NVS Records ---
  prefs.begin("records", false);   // namespace "records"
  loadAllTimeRecords();            // charge min/max absolus depuis NVS

  // On initialise les stats courantes avec NAN
  ST_tempExt.cur = NAN;  ST_humExt.cur = NAN;  ST_press.cur = NAN;
  ST_batt.cur    = NAN;  ST_solar.cur = NAN;

  // Optionnel: afficher ce qui a été chargé
  Serial.printf("[Records] t(min=%.1f,max=%.1f) h(min=%.0f,max=%.0f) p(min=%.1f,max=%.1f)\n",
                ST_tempExt.minAll, ST_tempExt.maxAll, ST_humExt.minAll, ST_humExt.maxAll,
                ST_press.minAll, ST_press.maxAll);
  Serial.printf("[Records] gust(max=%.1f) dir=%d\n", GUST_maxAll, GUST_dirAll);


  apiRefreshConfig();  // charge API_URL, API_KEY, STATION_ID, etc.

 
  Wire.begin();
  
    // ---- SOFT RESET BMP280 AU DEMARRAGE ----
  bmp_write8(0xE0, 0xB6);   // Soft reset officiel BMP280
  delay(10);                // Attendre le reset interne
  bmp_ok = initBMP280();    // Ré‑initialise complètement le capteur

  if (!bmp_ok) {
      Serial.println("[BMP280] Soft reset: KO");
      app_logf("[BMP280] Soft reset: KO");
  } else {
      Serial.println("[BMP280] Soft reset: OK");
      app_logf("[BMP280] Soft reset: OK");
  }
  // --- Time/NTP au boot ---
  loadNtpTzFromNvs();                   // récupère ce qui a été enregistré précédemment
  applyTimeConfig(g_timezone, g_ntp_server);  // applique au système (TZ + serveur NTP)

  if (!rtc.begin()) {
    Serial.println("RTC non détecté !");
    app_logf("RTC non détecté !");
    //while (1);
  }
  
  // ---- VERIFICATION RTC ET REMISE A L’HEURE AUTOMATIQUE ----
  DateTime nowRTC = rtc.now();

  // Si l'heure est invalide : DS1307 => 2000-01-01 après un freeze I2C
  if (nowRTC.year() < 2024) {       // seuil arbitraire pour détecter une date cassée
      Serial.println("[RTC] Heure invalide -> Remise à l'heure NTP.");
      app_logf("[RTC] Heure invalide -> Remise à l'heure NTP.");

      setRTCFromNTP();              // ta fonction déjà existante
      delay(200);

      // Vérification
      DateTime check_rtc = rtc.now();
      Serial.printf("[RTC] Nouvelle heure : %04d-%02d-%02d %02d:%02d:%02d\n",
          check_rtc.year(), check_rtc.month(), check_rtc.day(),
          check_rtc.hour(), check_rtc.minute(), check_rtc.second());
  }

  // Capturer le timestamp de démarrage (boot time)
  g_boot_time = time(nullptr);
  if (g_boot_time > 0) {
    struct tm* ptm = localtime(&g_boot_time);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ptm);
    Serial.printf("[BOOT] Boot time: %s (epoch: %ld)\n", buf, g_boot_time);
    app_logf("[BOOT] Boot time: %s (epoch: %ld)", buf, g_boot_time);
  } else {
    Serial.println("[BOOT] NTP/RTC not yet ready, boot_time will be 0");
  }

    // Initialisation du capteur BMP280
  if (!initBMP280()) 
  {
      Serial.println(F("Failed to initialize BMP280. Check wiring."));
      app_logf("Failed to initialize BMP280. Check wiring.");
      bmp_ok = false;
    
  }else
  {
      bmp_ok = true;
      noteI2C_OK();
      Serial.println(F("BMP280 initialized successfully."));
      app_logf("BMP280 initialized successfully.");
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
  pinMode(ANEMO_PIN, INPUT_PULLUP);   // interrupteur Reed à la pin 8
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
     

  
  // --- Wi‑Fi AP+STA avec NVS & non-bloquant ---
  WiFi.persistent(false);
  WiFi.onEvent(onWiFiEvent);
  WiFi.mode(WIFI_AP_STA);

  // Charger secrets (NVS) et démarrer l’AP local pour l’écran/admin
  loadApFromNvs();
  loadAuthFromNvs();
  startLocalAP();

  // Lancer la connexion STA (réseau maison) sans bloquer
  startStaFromNVS();

  // Option conso/perf (au choix) :
  WiFi.setSleep(false); // perf réseau (écran 30 s OK) ; passer true si tu veux économiser
  
  tensions_begin_async(1000); // 1 échantillon par seconde suffit largement

  //Activation de l'envoi API au démarrage (sera réajusté après chargement NVS)
  activation_envoi_api = 1;
  
// --- Modules + activation API : préférer NVS, fallback unique DB ---
{
  Modules bootMods{};
  int apiActive = 0;


bool haveNvs = loadModulesFromNvs(bootMods, apiActive);

if (!haveNvs) {

  Serial.println("⚠️ Modules NVS absents -> valeurs par défaut");

  // valeurs par défaut
  bootMods = Modules{
    /*bmp280*/1,
    /*dht22*/1,
    /*sht40*/0,
    /*anemo*/1,
    /*girou*/1,
    /*pluvio*/1,
    /*tension*/1,
    /*bitvie*/1
  };

  apiActive = 0;

  // sauvegarde en NVS
  saveModulesToNvs(bootMods, apiActive);
}


  // Appliquer la config au runtime
  applyModuleChange(mods, bootMods);
  mods = bootMods;

  module_bmp280 = bootMods.bmp280;
  module_dht22  = bootMods.dht22;
  module_sht40  = bootMods.sht40;
  module_anemo  = bootMods.anemo;
  module_girou  = bootMods.girou;
  module_pluvio = bootMods.pluvio;
  module_tension= bootMods.tension;
  module_bitvie = bootMods.bitvie;

  activation_envoi_api = apiActive;
  Serial.printf("activation_envoi_api chargé: %d\n", activation_envoi_api);
  // Réactivation de l'envoi API au démarrage en fonction de la config NVS
  Serial.printf("activation_envoi_api initialisé à: %d\n", activation_envoi_api);
  // Temporairement désactiver l'envoi API pour éviter les blocages du serveur web

}

loadOffsetsFromNvs();
// --- ROUTES HTTP (à mettre DANS setup(), après connectWifiFromDB(), avant server.begin()) ---
// Routes de base
server.on("/", handleRoot);
server.on("/data", handleData);
server.on("/settime", handleSetTime);
server.on("/etatcapteurs", HTTP_GET, handleEtatCapteurs);
server.on("/status.json", HTTP_GET, handleStatusJson);


// Protéger la page config
server.on("/config", HTTP_GET, []() {
  if (!requireAdminAuth()) return;
  File file = LittleFS.open("/config.html", "r");
  if (file) { server.streamFile(file, "text/html"); file.close(); }
  else      { server.send(404, "text/plain", "Page config introuvable"); }
});

// Exemple : protéger aussi les postes existants si tu veux
// server.on("/config.save", HTTP_POST, { if(!requireAdminAuth()) return; ... });

  
// Lire la config Auth/AP (masquée) pour l'UI
server.on("/config/auth", HTTP_GET, []() {
  if (!requireAdminAuth()) return;

  StaticJsonDocument<512> j;
  JsonObject auth = j.createNestedObject("auth");
  auth["viewer_user"] = g_auth.viewerUser;
  
  // Créer des chaînes de masquage
  String viewerPassMask;
  for (size_t i = 0; i < g_auth.viewerPass.length(); i++) viewerPassMask += '*';
  auth["viewer_pass"] = viewerPassMask;
  
  auth["admin_user"]  = g_auth.adminUser;
  String adminPassMask;
  for (size_t i = 0; i < g_auth.adminPass.length(); i++) adminPassMask += '*';
  auth["admin_pass"] = adminPassMask;

  JsonObject ap = j.createNestedObject("ap");
  ap["ssid"] = g_ap.ssid;
  String apPassMask;
  for (size_t i = 0; i < g_ap.pass.length(); i++) apPassMask += '*';
  ap["pass"] = apPassMask;
  ap["chan"] = g_ap.channel;
  ap["maxc"] = g_ap.maxConn;

  String out; serializeJson(j, out);
  server.send(200, "application/json", out);
});

// Sauver (partiel) Auth/AP et redémarrer AP si besoin
server.on("/config/auth.save", HTTP_POST, []() {
  if (!requireAdminAuth()) return;

  bool apChanged = false, authChanged = false;

  // Auth viewer/admin
  String viewer_user = server.arg("viewer_user");
  String viewer_pass = server.arg("viewer_pass");
  String admin_user  = server.arg("admin_user");
  String admin_pass  = server.arg("admin_pass");

  if (viewer_user.length()) { g_auth.viewerUser = viewer_user; authChanged = true; }
  if (viewer_pass.length()) { g_auth.viewerPass = viewer_pass; authChanged = true; }
  if (admin_user.length())  { g_auth.adminUser  = admin_user;  authChanged = true; }
  if (admin_pass.length())  { g_auth.adminPass  = admin_pass;  authChanged = true; }

  if (authChanged) {
    prefsAuth.begin("auth", /*rw=*/false);
    prefsAuth.putString("viewer_user", g_auth.viewerUser);
    prefsAuth.putString("viewer_pass", g_auth.viewerPass);
    prefsAuth.putString("admin_user",  g_auth.adminUser);
    prefsAuth.putString("admin_pass",  g_auth.adminPass);
    prefsAuth.end();
  }

  // AP
  String ap_ssid = server.arg("ap_ssid");
  String ap_pass = server.arg("ap_pass");
  String ap_chan = server.arg("ap_chan");
  String ap_maxc = server.arg("ap_maxc");

  if (ap_ssid.length()) { g_ap.ssid = ap_ssid; apChanged = true; }
  if (ap_pass.length()) {
    if (ap_pass.length() < 8) { server.send(400, "text/plain", "AP pass >= 8"); return; }
    g_ap.pass = ap_pass; apChanged = true;
  }
  if (ap_chan.length()) {
    int ch = ap_chan.toInt();
    if (ch < 1 || ch > 13) { server.send(400, "text/plain", "AP chan 1..13"); return; }
    g_ap.channel = (uint8_t)ch; apChanged = true;
  }
  if (ap_maxc.length()) {
    int mc = ap_maxc.toInt();
    if (mc < 1 || mc > 4) { server.send(400, "text/plain", "AP max 1..4"); return; }
    g_ap.maxConn = (uint8_t)mc; apChanged = true;
  }

  if (apChanged) {
    prefsAp.begin("ap", /*rw=*/false);
    prefsAp.putString("ssid", g_ap.ssid);
    prefsAp.putString("pass", g_ap.pass);
    prefsAp.putUChar("chan", g_ap.channel);
    prefsAp.putUChar("maxc", g_ap.maxConn);
    prefsAp.end();

    // Redémarrer l'AP pour appliquer
    WiFi.softAPdisconnect(true);
    delay(100);
    startLocalAP();
  }

  server.send(200, "application/json", "{\"ok\":true}");
});


server.on("/wifi/scan.json", HTTP_GET, []() {
  if (!requireAdminAuth()) return;

  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED || n == WIFI_SCAN_RUNNING) {
    WiFi.scanNetworks(true, /*show_hidden=*/false); // asynchrone
    server.send(202, "application/json", "{\"status\":\"scanning\"}");
    return;
  }

  StaticJsonDocument<2048> j;
  JsonArray arr = j.createNestedArray("nets");
  for (int i = 0; i < n; ++i) {
    JsonObject o = arr.createNestedObject();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["chan"] = WiFi.channel(i);
    o["enc"]  = WiFi.encryptionType(i); // valeur numérique
  }
  WiFi.scanDelete();
  String out; serializeJson(j, out);
  server.send(200, "application/json", out);
});


// --- Page sous-config girouette ---
server.on("/config/vane", HTTP_GET, []() {
  File f = LittleFS.open("/config_vane.html", "r");
  if (f) { server.streamFile(f, "text/html"); f.close(); }
  else   { server.send(404, "text/plain", "Page girouette introuvable"); }
});

// --- JSON live des entrées de la girouette ---
server.on("/vane/inputs.json", HTTP_GET, []() {
  StaticJsonDocument<512> doc;
  JsonObject j = doc.createNestedObject("vane");

  // Etat bouton calibration (LOW = appuyé car INPUT_PULLUP)
  j["button"] = (digitalRead(PIN_BTN_CAL) == LOW);

  // Offset Nord courant (on utilise ta variable g_northOffsetDeg)
  j["offset_deg"] = g_northOffsetDeg;

  // Pins: N, NE, E, SE, S, SW, W, NW
  const char* NAMES[8] = {"N","NE","E","SE","S","SW","W","NW"};
  JsonArray pins = j.createNestedArray("pins");
  for (int i = 0; i < 8; ++i) {
    JsonObject o = pins.createNestedObject();
    o["name"] = NAMES[i];
    int raw = digitalRead(PINS_GIROUETTE[i]);  // HIGH/LOW
    o["raw"] = raw;
    // Tu as configuré invertLogic=true + INPUT_PULLUP -> contact actif = LOW
    o["active"] = (raw == LOW);
  }

  // Angle et nom cardinal courants (fenêtre courte pour limiter la latence)
  float angle = girouette.readAngle(24);
  if (!isnan(angle)) {
    j["angle"] = angle;
    j["card"]  = girouette.readName(24);
  } else {
    j["angle"] = nullptr;
    j["card"]  = "—";
  }

  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
});


server.on("/vane/calibrate", HTTP_POST, []() {
  float angle = girouette.readAngle(24);
  if (!isnan(angle)) {
    // nouveau offset : on veut que l'angle actuel devienne 0°
    g_northOffsetDeg = wrap360f(g_northOffsetDeg - angle);
    girouette.setNorthOffsetDegrees(g_northOffsetDeg);
    server.send(200, "application/json", "{\"ok\":true}");
    Serial.printf("[Girouette] Calibration Nord API OK (offset=%.1f°)\n", g_northOffsetDeg);
  } else {
    server.send(500, "application/json", "{\"ok\":false}");
    app_logf("[Girouette] Calibration API ignorée: angle invalide");
    Serial.println("[Girouette] Calibration API ignorée: angle invalide");
  }
});


// NotFound -> sert /images/... avec cache, sinon 404
server.onNotFound([]() {
  String uri = server.uri();
  if (uri.startsWith("/images/")) {
    sendWithCache(uri);
    return;
  }
  server.send(404, "text/plain", "Not found");
});



// Protéger la mise à jour du Wi-Fi maison
server.on("/config/network", HTTP_POST, []() {

  if (!requireAdminAuth()) return;

  Preferences prefs;
  prefs.begin("wifi", false);

  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");

  // Mise à jour depuis le formulaire
  if (server.hasArg("ssid")) {
    ssid = server.arg("ssid");
  }

  if (server.hasArg("wifi_password")) {
    String p = server.arg("wifi_password");
    if (p != "") pass = p;
  }

  // Sauvegarde
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);

  prefs.end();

  Serial.println("✅ WiFi sauvegardé en NVS");

  // Reconnexion si changement
  WiFi.disconnect(true, true);
  delay(300);
  WiFi.begin(ssid.c_str(), pass.c_str());

  server.send(200, "text/plain", "OK");
});




// au début du fichier :
extern void apiRefreshConfig(); // déjà déclaré dans api.h

server.on("/config/api", HTTP_POST, []() {

  // 🔹 Lire actuel depuis NVS
  Preferences prefs;
  prefs.begin("api", false);

  String base = prefs.getString("base", "");
  String token = prefs.getString("key", "");
  int stationId = prefs.getInt("id", 1);

  // 🔹 Mise à jour depuis formulaire
  if (server.hasArg("api_url")) {
    base = server.arg("api_url");
  }

  if (server.hasArg("api_token")) {
    token = server.arg("api_token");
  }

  // 🔹 Activation API (RAM uniquement)
  activation_envoi_api = server.hasArg("api_enabled") ? 1 : 0;

  
  Preferences prefsMods;
  prefsMods.begin("modules", false);

  prefsMods.putInt("api", activation_envoi_api);

  prefsMods.end();

  Serial.printf("activation_envoi_api = %d\n", activation_envoi_api);

  // 🔹 Sauvegarde en NVS
  prefs.putString("base", base);
  prefs.putString("key", token);
  prefs.putInt("id", stationId);

  prefs.end();

  // 🔹 Mettre à jour les variables runtime (IMPORTANT)
  API_URL = base + "/stationdirect/" + String(stationId);
  API_URL_ETATSTATION = base + "/etatstationmeteo/1";

  server.send(200, "text/plain", "OK");
});




server.on("/config/modules", HTTP_POST, []() {
  // Lire les champs du formulaire
  Modules n{};
  n.bmp280  = server.hasArg("bmp280_enabled") ? 1 : 0;
  n.dht22   = server.hasArg("dht22_enabled")  ? 1 : 0;
  n.sht40   = server.hasArg("sht40_enabled")  ? 1 : 0;
  n.anemo   = server.hasArg("anemo_enabled")  ? 1 : 0;
  n.girou   = server.hasArg("vane_enabled")   ? 1 : 0;  // "vane" côté UI
  n.pluvio  = server.hasArg("rain_enabled")   ? 1 : 0;
  n.tension = server.hasArg("tension_enabled")? 1 : 0;
  n.bitvie  = server.hasArg("bitvie_enabled") ? 1 : 0;

  // Appliquer immédiatement
  applyModuleChange(mods, n);
  mods = n;

  module_bmp280 = n.bmp280;
  module_dht22  = n.dht22;
  module_sht40  = n.sht40;
  module_anemo  = n.anemo;
  module_girou  = n.girou;
  module_pluvio = n.pluvio;
  module_tension= n.tension;
  module_bitvie = n.bitvie;

  // Persister en NVS
  saveModulesToNvs(n, activation_envoi_api);

  // (optionnel) Maintenir la DB en phase pour compat si un outil externe la lit
  // updateModulesInDB(1, n.bmp280, n.dht22, n.sht40, n.anemo, n.girou, n.pluvio, n.tension, n.bitvie);

  server.send(200, "text/plain", "OK");
});


// /style (MIME + cache)
server.on("/style", HTTP_GET, []() {
  const char* path = "/style.css";
  if (!LittleFS.exists(path)) {
    server.send(404, "text/plain", "style.css introuvable");
    return;
  }
  File f = LittleFS.open(path, "r");
  server.sendHeader("Cache-Control", "public, max-age=604800, immutable");
  server.streamFile(f, "text/css");
  f.close();
});

server.on("/script", HTTP_GET, []() {
  const char* path = "/script.js";
  if (!LittleFS.exists(path)) {
    server.send(404, "text/plain", "script.js introuvable");
    return;
  }
  File f = LittleFS.open(path, "r");
  server.sendHeader("Cache-Control", "public, max-age=604800, immutable");
  server.streamFile(f, "text/javascript");
  f.close();
});

//javascript configjs

server.on("/configjs", HTTP_GET, []() {
  const char* path = "/configjs.js";
  if (!LittleFS.exists(path)) { server.send(404, "text/plain", "configjs.js introuvable"); return; }
  File f = LittleFS.open(path, "r");
  // ❗ pas de cache pour ce fichier : on veut les derniers correctifs JS
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.streamFile(f, "text/javascript");
  f.close();
});


// /infos
server.on("/infos", HTTP_GET,  [] (){
  File file = LittleFS.open("/infos.html", "r");
  if (file) {
    server.streamFile(file, "text/html");
    file.close();
  } else {
    server.send(404, "text/plain", "Page d'informations introuvable");
  }
});

// /update_config
server.on("/update_config", HTTP_POST, [] () {

  int bmp280 = server.hasArg("bmp280") ? 1 : 0;
  int dht22  = server.hasArg("dht22")  ? 1 : 0;
  int anemo  = server.hasArg("anemo")  ? 1 : 0;
  int girou  = server.hasArg("girou")  ? 1 : 0;
  int pluvio = server.hasArg("pluvio") ? 1 : 0;
  int tension= server.hasArg("tension")? 1 : 0;
  int bitvie = server.hasArg("bitvie") ? 1 : 0;
  int api    = server.hasArg("api")    ? 1 : 0;
  int sht40  = server.hasArg("sht40")  ? 1 : 0;

  // ✅ création de la structure
  Modules m;
  m.bmp280 = bmp280;
  m.dht22  = dht22;
  m.sht40  = sht40;
  m.anemo  = anemo;
  m.girou  = girou;
  m.pluvio = pluvio;
  m.tension= tension;
  m.bitvie = bitvie;

  // ✅ sauvegarde NVS
  saveModulesToNvs(m, api);

  // ✅ mise à jour runtime
  activation_envoi_api = api;

  Serial.println("✅ Configuration modules sauvegardée (NVS)");

  server.send(200, "text/html",
              "<h3>Configuration mise à jour !</h3><a href='/config'>Retour</a>");
});



// GET /config.json
server.on("/config.json", HTTP_GET,  [] () {

  Preferences prefsWifi;
  prefsWifi.begin("wifi", true);

  String ssid = prefsWifi.getString("ssid", "");
  String pass = prefsWifi.getString("pass", "");

  prefsWifi.end();

  Preferences prefsApi;
  prefsApi.begin("api", true);

  String api_url = prefsApi.getString("base", "");
  String token   = prefsApi.getString("key", "");
  int stationId  = prefsApi.getInt("id", 1);

  prefsApi.end();

  StaticJsonDocument<512> doc;

  doc["ssid_wifi"]  = ssid;
  doc["pass_wifi"]  = String(pass.length(), '*'); // masque
  doc["IP_WIFI"]    = "dhcp"; // tu peux adapter si besoin
  doc["ID_STATION"] = stationId;
  doc["adresse_api"]= api_url;
  doc["token"]      = token;

  // ✅ IMPORTANT : on utilise la variable RAM
  doc["activation_envoi_api"] = activation_envoi_api;

  // NTP / timezone inchangé
  doc["ntp_server"] = g_ntp_server;
  doc["timezone"]   = g_timezone;

  String out;
  serializeJson(doc, out);

  server.send(200, "application/json", out);
});



// POST /config.save
server.on("/config.save", HTTP_POST, [] () {

  // 🔹 NVS WiFi
  Preferences prefsWifi;
  prefsWifi.begin("wifi", false);

  String ssid = prefsWifi.getString("ssid", "");
  String pass = prefsWifi.getString("pass", "");

  // 🔹 NVS API
  Preferences prefsApi;
  prefsApi.begin("api", false);

  String api_url = prefsApi.getString("base", "");
  String token   = prefsApi.getString("key", "");
  int stationId  = prefsApi.getInt("id", 1);

  // 🔹 récupérer valeurs envoyées
  if (server.hasArg("ssid_wifi")) ssid = server.arg("ssid_wifi");

  if (server.hasArg("pass_wifi")) {
    String p = server.arg("pass_wifi");
    if (p != "") pass = p;
  }

  if (server.hasArg("adresse_api")) api_url = server.arg("adresse_api");
  if (server.hasArg("token"))       token   = server.arg("token");

  if (server.hasArg("ID_STATION")) {
    stationId = server.arg("ID_STATION").toInt();
  }

  if (server.hasArg("activation_envoi_api")) {
    activation_envoi_api = server.arg("activation_envoi_api").toInt();
  }

  // 🔹 Sauvegarde NVS
  prefsWifi.putString("ssid", ssid);
  prefsWifi.putString("pass", pass);

  prefsApi.putString("base", api_url);
  prefsApi.putString("key", token);
  prefsApi.putInt("id", stationId);

  prefsWifi.end();
  prefsApi.end();

  // 🔹 Mise à jour runtime API (IMPORTANT)
  API_URL = api_url + "/stationdirect/" + String(stationId);
  API_URL_ETATSTATION = api_url + "/etatstationmeteo/1";

  // --- NTP/TZ ---
  if (server.hasArg("ntp_server")) {
    g_ntp_server = server.arg("ntp_server");
  }

  if (server.hasArg("timezone")) {
    g_timezone = server.arg("timezone");
  }

  applyTimeConfig(g_timezone, g_ntp_server);
  saveNtpTzToNvs();

  // 🔹 reconnect WiFi si changement
  WiFi.disconnect(true, true);
  delay(300);
  WiFi.begin(ssid.c_str(), pass.c_str());

  Serial.println("✅ Configuration sauvegardée (NVS)");

  server.send(200, "application/json", "{\"ok\":true}");
});




// --- Endpoint JSON live des tensions ---
server.on("/adc/live", HTTP_GET, []() {
  float vAdcSolar = readAdcAveraged(SOLAR_PIN, 3.6f, 12);
  float vAdcBatt  = readAdcAveraged(BATTERY_PIN, 3.6f, 12);
  int   rawSolar  = analogRead(SOLAR_PIN);
  int   rawBatt   = analogRead(BATTERY_PIN);

  const float kSolar = (100000.0f + 47000.0f) / 47000.0f; // 3.1277 (si montage prévu)
  const float kBatt  = (200000.0f + 100000.0f) / 100000.0f; // 3.0 (si 200k/100k)

  StaticJsonDocument<256> doc;
  JsonObject solar = doc.createNestedObject("solar");
  solar["raw"]   = rawSolar;
  solar["v_adc"] = vAdcSolar;
  solar["v_corr"]= vAdcSolar * kSolar;
  solar["sat"]   = (rawSolar >= 4090);  // ⚠️ butée

  JsonObject batt = doc.createNestedObject("batt");
  batt["raw"]   = rawBatt;
  batt["v_adc"] = vAdcBatt;
  batt["v_corr"]= vAdcBatt * kBatt;
  batt["sat"]   = (rawBatt >= 4090);

  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
});

server.on("/adc.html", HTTP_GET, []() {
  const char* path = "/adc.html";
  if (!LittleFS.exists(path)) {
    server.send(404, "text/plain", "adc.html introuvable");
    return;
  }
  File f = LittleFS.open(path, "r");
  server.sendHeader("Cache-Control", "no-store"); // éviter cache pendant tests
  server.streamFile(f, "text/html");
  f.close();
});


// dans setupLocalStationApiHandler(WebServer &server)
server.on("/api/push/status", HTTP_GET, [] (){
  StaticJsonDocument<512> j;
  j["row_code"] = g_lastPushRow.code;
  j["row_body"] = g_lastPushRow.body;
  j["row_age_ms"] = (unsigned long)(millis() - g_lastPushRow.ts_ms);
  j["row_endpoint"] = g_lastPushRow.endpoint;
  j["etat_code"] = g_lastPushEtat.code;
  j["etat_body"] = g_lastPushEtat.body;
  j["etat_age_ms"] = (unsigned long)(millis() - g_lastPushEtat.ts_ms);
  j["etat_endpoint"] = g_lastPushEtat.endpoint;
  String out; serializeJson(j, out);
  server.send(200, "application/json", out);
});




  // Routes HTTP pour consulter/vider
  server.on("/log.txt", HTTP_GET, [] ()  {
    String body;
    if (!log_read(body, 16*1024)) body = "(journal indisponible)";
    server.sendHeader("Cache-Control","no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma","no-cache");
    server.send(200, "text/plain", body);
  });
  server.on("/log/clear", HTTP_POST, [] () {
    log_clear();
    server.sendHeader("Cache-Control","no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma","no-cache");
    server.send(200, "application/json", "{\"ok\":true}");
  });

  
// Retourne les N dernières lignes du journal depuis le ring-buffer (log_read)
server.on("/log/tail", HTTP_GET,  []() {
  // 1) Nombre de lignes demandé (par défaut 250)
  int n = 250;
  if (server.hasArg("n")) {
    int tmp = server.arg("n").toInt();
    if (tmp >= 10 && tmp <= 5000) n = tmp;
  }

  // 2) Lire le journal depuis le ring-buffer (pas besoin d'attendre le flush SPIFFS)
  String body;
  if (!log_read(body, 64 * 1024)) {           // lis jusqu’à 64 kB de mémoire tampon
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.send(200, "text/plain; charset=utf-8", "(journal indisponible)");
    return;
  }

  // 3) Extraire les N dernières lignes côté ESP (plus robuste que côté JS)
  int count = 0;
  for (int i = body.length() - 1; i >= 0 && count < n; --i) {
    if (body[i] == '\n') count++;
  }

  int pos = 0;
  if (count >= n) {
    int need = n;
    for (int i = body.length() - 1; i >= 0; --i) {
      if (body[i] == '\n' && --need == 0) { pos = i + 1; break; }
    }
  }

  String tail = body.substring(pos);
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.send(200, "text/plain; charset=utf-8", tail.length() ? tail : "(journal vide)");
});

// ---- Liste des fichiers de logs (archives) ----
server.on("/logs/list", HTTP_GET, []() {
  String out = "[";
  bool first = true;
  File root = LittleFS.open("/");
  while (true) {
    File f = root.openNextFile();
    if (!f) break;
    String name = f.name();
    size_t size = f.size();
    f.close();

    if (name.startsWith("/log") && name.endsWith(".txt")) {
      if (!first) out += ",";
      first = false;
      out += "{\"name\":\"" + name + "\",\"size\":" + String((unsigned)size) + "}";
    }
  }
  out += "]";
  server.send(200, "application/json", out);
});

// ---- Télécharger un log précis ----
// GET /logs/get?file=/log-20260203.txt
server.on("/logs/get", HTTP_GET, []() {
  if (!server.hasArg("file")) { server.send(400, "text/plain", "file param missing"); return; }
  String file = server.arg("file");
  if (!file.startsWith("/")) file = "/" + file;
  if (!LittleFS.exists(file)) { server.send(404, "text/plain", "not found"); return; }
  String content;
  if (!log_read_file(file.c_str(), content, 0)) { server.send(500, "text/plain", "read error"); return; }
  server.send(200, "text/plain; charset=utf-8", content);
});

// --- Données simplifiées pour l'écran (ESP8266) : protégé "viewer" ---
server.on("/screen/data.json", HTTP_GET, []() {
  if (!requireViewerAuth()) return;

  StaticJsonDocument<512> doc;
  doc["t"]    = g_snap.temp_bmp;        // °C (BMP)
  doc["h"]    = g_snap.hum;             // % (DHT)
  doc["p"]    = g_snap.press_hPa;       // hPa
  doc["w"]    = g_snap.wind;            // km/h
  doc["g"]    = g_snap.gust;            // km/h
  doc["dir"]  = g_snap.dir_deg;         // deg (0..359, -1 = null)
  doc["rain"] = g_snap.rain_cum;        // mm cumulés
  doc["vb"]   = g_snap.batt_v;          // V
  doc["vs"]   = g_snap.solar_v;         // V
  doc["ts"]   = g_snap.datetime;        // "YYYY-MM-DD HH:MM:SS"

  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
});



// --- Test d'authentification (diagnostic rapide) ---
server.on("/auth/test", HTTP_GET, []() {
  if (server.authenticate(g_auth.adminUser.c_str(), g_auth.adminPass.c_str())) {
    server.send(200, "application/json", "{\"role\":\"admin\"}");
    return;
  }
  if (server.authenticate(g_auth.viewerUser.c_str(), g_auth.viewerPass.c_str())) {
    server.send(200, "application/json", "{\"role\":\"viewer\"}");
    return;
  }
  server.requestAuthentication(); // 401
});


// --- GET /config/offsets : lire les offsets
server.on("/config/offsets", HTTP_GET, []() {
  if (!requireAdminAuth()) return;
  StaticJsonDocument<256> j;
  j["t_bmp"]   = ofs.t_bmp;
  j["t_dht"]   = ofs.t_dht;
  j["h_dht"]   = ofs.h_dht;
  j["press"]   = ofs.press;
  j["wind"]    = ofs.wind;
  j["t_sht40"] = ofs.t_sht40;
  String out; serializeJson(j, out);
  server.send(200, "application/json", out);
});

// --- POST /config/offsets.save : enregistrer offsets
server.on("/config/offsets.save", HTTP_POST, []() {
  if (!requireAdminAuth()) return;
  auto readF = [&](const char* key, float &dst){
    if (server.hasArg(key)) { dst = server.arg(key).toFloat(); }
  };
  readF("t_bmp",   ofs.t_bmp);
  readF("t_dht",   ofs.t_dht);
  readF("h_dht",   ofs.h_dht);
  readF("press",   ofs.press);
  readF("wind",    ofs.wind);
  readF("t_sht40", ofs.t_sht40);
  saveOffsetsToNvs(ofs);
  app_logf("[Offsets] t_bmp=%.2f t_dht=%.2f h_dht=%.2f press=%.2f wind=%.2f t_sht40=%.2f",
           ofs.t_bmp, ofs.t_dht, ofs.h_dht, ofs.press, ofs.wind, ofs.t_sht40);
  server.send(200, "application/json\"ok\":true}");
});

// --- POST /records/reset : reset min/max (day / all / both)
server.on("/records/reset", HTTP_POST, []() {
  if (!requireAdminAuth()) return;
  String scope = server.hasArg("scope") ? server.arg("scope") : "both";

  if (scope == "day" || scope == "both") {
    ST_tempExt.resetDaily(); ST_humExt.resetDaily(); ST_press.resetDaily();
    ST_batt.resetDaily();    ST_solar.resetDaily();
    GUST_maxDay = 0.0f; GUST_dirDay = -1;
  }
  if (scope == "all" || scope == "both") {
    Preferences prefs; 
    // Efface les records all‑time en NVS
    prefs.begin("records", /*rw=*/false);
    prefs.clear();
    prefs.end();
    // Réinitialise les copies en RAM
    ST_tempExt.minAll = ST_tempExt.maxAll = NAN;
    ST_humExt.minAll  = ST_humExt.maxAll  = NAN;
    ST_press.minAll   = ST_press.maxAll   = NAN;
    ST_batt.minAll    = ST_batt.maxAll    = NAN;
    ST_solar.minAll   = ST_solar.maxAll   = NAN;
    GUST_maxAll = 0.0f; GUST_dirAll = -1;
    g_recordsDirty = false;
  }
  app_logf("[Records] Reset scope=%s", scope.c_str());
  server.send(200, "application/json", "{\"ok\":true}");
});


// --- Données complètes pour écran/dash (protégé "viewer") ---
server.on("/screen/full.json", HTTP_GET, []() {
  if (!requireViewerAuth()) return;

  // Taille un peu plus large que 512 pour être à l’aise
  StaticJsonDocument<1536> doc;

  // 1) Mesures (corrigées, depuis g_snap)
  doc["t_bmp"] = g_snap.temp_bmp;        // °C BMP
  doc["t_dht"] = g_snap.temp_dht;        // °C DHT (si alimenté)
  doc["hum"]   = g_snap.hum;             // % (0..100)
  doc["press"] = g_snap.press_hPa;       // hPa
  doc["dew"]   = g_snap.dew;             // point de rosée (°C)

  // Vent & pluie
  doc["wind"]  = g_snap.wind;            // km/h instantané
  doc["gust"]  = g_snap.gust;            // km/h rafale
  doc["dir"]   = g_snap.dir_deg;         // degrés (0..359, -1 si inconnu)
  doc["dirc"]  = degToCardinal(g_snap.dir_deg);  // libellé cardinal
  doc["rain_cum"]  = g_snap.rain_cum;    // mm cumulés (total courant)
  doc["rain_hour"] = rainHourMm();       // mm dernière heure
  doc["rain_day"]  = rainDayMm();        // mm du jour
  doc["rain_week"] = rainWeekMm();       // mm (semaine glissante)

  // Tensions
  doc["vb"] = g_snap.batt_v;             // V batterie
  doc["vs"] = g_snap.solar_v;            // V panneau

  // 2) Stats min/max (jour + all-time)
  JsonObject s = doc.createNestedObject("stats");
  s["t_min_day"] = ST_tempExt.minDay;    s["t_max_day"] = ST_tempExt.maxDay;
  s["t_min_all"] = ST_tempExt.minAll;    s["t_max_all"] = ST_tempExt.maxAll;
  s["h_min_day"] = ST_humExt.minDay;     s["h_max_day"] = ST_humExt.maxDay;
  s["h_min_all"] = ST_humExt.minAll;     s["h_max_all"] = ST_humExt.maxAll;
  s["p_min_day"] = ST_press.minDay;      s["p_max_day"] = ST_press.maxDay;
  s["p_min_all"] = ST_press.minAll;      s["p_max_all"] = ST_press.maxAll;
  s["vb_min_day"] = ST_batt.minDay;      s["vb_max_day"] = ST_batt.maxDay;
  s["vb_min_all"] = ST_batt.minAll;      s["vb_max_all"] = ST_batt.maxAll;
  s["vs_min_day"] = ST_solar.minDay;     s["vs_max_day"] = ST_solar.maxDay;
  s["vs_min_all"] = ST_solar.minAll;     s["vs_max_all"] = ST_solar.maxAll;
  // Rafales
  s["gust_max_day"] = GUST_maxDay;       s["gust_dir_day"] = GUST_dirDay;
  s["gust_max_all"] = GUST_maxAll;       s["gust_dir_all"] = GUST_dirAll;

  // 3) KPI réseau & horodatage
  int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -100;
  int pct  = rssiToPercent(rssi);
  int bars = pctToBars(pct);
  JsonObject net = doc.createNestedObject("net");
  net["wifi_rssi"]    = rssi;            // dBm
  net["wifi_percent"] = pct;             // 0..100
  net["wifi_bars"]    = bars;            // 0..4

  doc["ts"]       = g_snap.datetime;     // "YYYY-MM-DD HH:MM:SS"
  doc["uptime_s"] = (uint32_t)(millis() / 1000UL);
  doc["compteur"] = (uint32_t)compteur;  // ton compteur monotone

  // 4) Bonus: vent moyen 10 min
  doc["wind_avg10"] = windAvg10min();

  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
});


server.on("/sensor/bmp280/softreset", HTTP_POST, []()  {
  if (!requireAdminAuth()) return;          // protège l'action
  bmpReinitReq = true;
  server.send(202, "application/json", "{\"accepted\":true}");
});

server.on("/reboot", HTTP_POST, []() {
    server.send(200, "application/json", "{\"ok\":true}");
    delay(200);
    ESP.restart();
});

// --- Toggle Watchdog I²C (admin) ---
server.on("/i2c/wd", HTTP_POST, []() {
  if (!requireAdminAuth()) return;
  if (server.hasArg("enable")) {
    String v = server.arg("enable");
    g_i2cWdEnabled = (v == "1" || v == "true" || v == "on");
  }
  String out = String("{\"enabled\":") + (g_i2cWdEnabled ? "true" : "false") + "}";
  server.send(200, "application/json", out);
});

server.on("/diag/power", HTTP_GET, []() {
    String json = "{";

    json += "\"reset_core0\":" + String(diag_reset_reason_core0) + ",";
    json += "\"reset_core1\":" + String(diag_reset_reason_core1) + ",";
    json += "\"vcc_min\":" + String(diag_vcc_min,2) + ",";
    json += "\"vcc_max\":" + String(diag_vcc_max,2) + ",";
    json += "\"brownout_count\":" + String(diag_brownout_count) + ",";
    json += "\"free_heap_boot\":" + String(diag_free_heap_boot) + ",";
    json += "\"last_reboot_ms\":" + String(diag_last_reboot_ms);

    json += "}";
    server.send(200, "application/json", json);
});
// ElegantOTA integration commented out to avoid conflicts with ArduinoOTA
// ElegantOTA.begin(&server);
// ElegantOTA.onStart(...) { ... }
// ElegantOTA.onEnd(...) { ... }
// ElegantOTA.onProgress(...) { ... }



// ---- ArduinoOTA (single OTA method) ----
extern TaskHandle_t stationInfoTaskHandle;
//extern SemaphoreHandle_t g_dbMutex;
extern void isrAnemo(); // si tu utilises une ISR
extern void isrPluvio();

static bool dbMutexWasTaken = false;


ArduinoOTA.onStart([]() {
  Serial.println("ArduinoOTA start — preparing safe state...");
  app_logf("[ArduinoOTA] start — preparing safe state...");
  otaInProgress = true;

  // (Optionnel) détacher interruptions si tu veux éviter des burst:
  detachInterrupt(18); // anémo
  detachInterrupt(19); // pluvio

  if (stationInfoTaskHandle) vTaskSuspend(stationInfoTaskHandle);

});

ArduinoOTA.onEnd( []() {
  Serial.println("ArduinoOTA end — restoring...");
  app_logf("[ArduinoOTA] end — restoring...");
  // 1) Remonter DB (mutex pris seulement si nécessaire/possible)
  /*
  if (g_dbMutex && xSemaphoreTake(g_dbMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    if (!db_begin()) Serial.println("DB reopen failed");
    xSemaphoreGive(g_dbMutex);
  } else {
    Serial.println("DB mutex busy — deferred db_begin()");
    app_logf("[ArduinoOTA] DB mutex busy — deferred db_begin()");
  }
  */
  // 2) SPIFFS reste monté; si tu l'avais démonté, remonte ici.
  // if (!SPIFFS.begin(true)) Serial.println("SPIFFS remount failed");

  // 3) Re-attach ISR + reprendre tâches
  
  anemo_init(
      ANEMO_PIN,      // ton GPIO défini à 18
      0.6667f,
      0.0f,
      1,
      2000,
      100.0f
  );

  initPluviometre();  // remet en place l’ISR du pluviomètre
  
  // Dans onEnd() / onError(), si tu l’avais démonté :
  //if (!SPIFFS.begin(true)) Serial.println("SPIFFS remount failed");

  if (stationInfoTaskHandle) vTaskResume(stationInfoTaskHandle);

  otaInProgress = false;
});

ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
  Serial.printf("ArduinoOTA progress: %u/%u\n", progress, total);
  app_logf("ArduinoOTA progress: %u/%u\n", progress, total);
});


ArduinoOTA.onError([](ota_error_t error) {

  Serial.printf("ArduinoOTA Error[%u]\n", error);
  app_logf("ArduinoOTA Error[%u]\n", error);

  otaInProgress = false; // ✅ autorise reprise

  // ✅ Pas de DB → rien à rouvrir

  anemo_init(18, 0.6667f, 0.0f, 1, 2000, 100.0f);
  initPluviometre();

  if (stationInfoTaskHandle) vTaskResume(stationInfoTaskHandle);
});


  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.println(file.name());
    app_logf(file.name());
    file = root.openNextFile();
  }

  // dans setup(), après avoir initialisé `server` :
  setupLocalStationApiHandler(server);
  
  // Route version
  server.on("/api/version", HTTP_GET, []() {
    StaticJsonDocument<256> doc;
    doc["firmware"] = FIRMWARE_VERSION;
    doc["config_schema"] = CONFIG_SCHEMA_VERSION;
    doc["build_date"] = BUILD_DATE;
    String payload; serializeJson(doc, payload);
    server.send(200, "application/json", payload);
  });
  
  // fetch immédiat au démarrage
  startStationInfoBackgroundTask();

  
  // 5) 👉 DÉMARRER le serveur web
  server.begin();
  Serial.println("WebServer ready.");
  app_logf("WebServer ready.");

  // 6) 👉 DÉMARRER ArduinoOTA (après Wi‑Fi OK)
  ArduinoOTA.begin();
  Serial.println("ArduinoOTA ready.");
   app_logf("ArduinoOTA ready.");


  // Afficher l’état SPIFFS au boot
  app_logf("[LittleFS] total=%u used=%u free=%u",
    (unsigned)LittleFS.totalBytes(), (unsigned)LittleFS.usedBytes(),
    (unsigned)(LittleFS.totalBytes()-LittleFS.usedBytes()));


}



void loop() {
    static unsigned long previousTime = 0;  // Temps du dernier traitement
    unsigned long currentTime = millis();  // Temps actuel
    static unsigned long tDbHealth = 0;    // Ajout de la déclaration de tDbHealth
    //static unsigned long tPollMods = 0;    // Déclaration de tPollMods

    // Gestion des requêtes OTA via ElegantOTA (handled by webserver)
    // ElegantOTA tourne dans les callbacks du serveur HTTP, pas besoin d'handle() ici.

    // Gestion des requêtes du serveur web
    
    // Exemple dans loop():
    server.handleClient();      // OK
    ArduinoOTA.handle();        // doit rester réactif

    float v = lireVCC5();
    if (v > 0.01f) {
      if (v < diag_vcc_min) diag_vcc_min = v;
      if (v > diag_vcc_max) diag_vcc_max = v;

      // si tu gardes solaire() : adapte ce seuil à ton usage
      if (v < 4.65f) {
        diag_brownout_count++;
        app_logf("[POWER] LOW Vcc detecté : %.2f V (proxy solaire)", v);
      }
    }
    
    // Reconnexion STA non bloquante si déconnecté
    if (!g_staConnected && (long)(millis() - g_nextReconnectMs) >= 0) {
      startStaFromNVS();                    
      g_nextReconnectMs = millis() + g_backoffMs;
    }

    
  // 👉 Si OTA en cours : on ne fait **rien d'autre** pour éviter la faim CPU/mémoire
    if (otaInProgress) {
      delay(1); // micro pause coopérative
      return;   // quitte la loop ici
    }


    DateTime now = rtc.now();
        sprintf(datetime, "%04d-%02d-%02d %02d:%02d:%02d",
          now.year(), now.month(), now.day(),
          now.hour(), now.minute(), now.second());

    
    // Reset journalier à minuit (s'appuie sur lastDaySeen)
    resetDailyStatsAtMidnightIfNeeded(now);

    /*    
    if (millis() - tDbHealth >= 10000) {       // toutes les 5 s
      if (!otaInProgress) {
          if (!pushBusy) {
              db_reopen_if_needed("/station.db");
          }
                    // réouvre seulement si nécessaire
      } else {
        Serial.println("OTA in progress — skipping db_reopen_if_needed");
        app_logf("OTA in progress — skipping db_reopen_if_needed");
      }
      tDbHealth = millis();
    }
    */
    
    // ----- 1) Historique vent pour moyenne 10 min -----
    if (millis() - t_lastWindPush >= 1000) { // on pousse 1 échantillon par seconde
      t_lastWindPush = millis();
      g_windBuf[g_windIdx] = vitesse; // vitesse instantanée km/h
      g_windIdx = (g_windIdx + 1) % WIND_BUF_SECS;
      if (g_windIdx == 0) g_windFilled = true;
    }

    // ----- 2) Snapshots pluie -----
    g_rainCum_now = quantite; // cumul courant en mm

    // Marque horaire pour "il y a 1 heure" (simple: toutes les 3600 s, on recale)
    if (millis() - t_lastHourMark >= 3600000UL) { // 1 h
      t_lastHourMark = millis();
      g_rainCum_t0h = g_rainCum_now;  // cliché du cumul à t-1h
    }

    // Détection de minuit local (via RTC)
   
    if (lastDaySeen != now.day()) {
      // On détecte un changement de jour -> on vient de passer minuit
      lastDaySeen = now.day();
      g_rainCum_midnight = g_rainCum_now; // cliché à 00:00
    }

    // Semaine glissante (simplifiée : recale toutes 24h pour t-7j)
    static uint8_t lastWeekRebaseDay = 255;
    if (lastWeekRebaseDay != now.day()) {
      lastWeekRebaseDay = now.day();
      // Approche simple : si tu veux être exact au jour près, tu peux conserver un tableau de 7 clichés quotidiens.
      // Ici on recale g_rainCum_t0w une fois par jour : après 7 jours, ça donne la semaine glissante approchée.
      // (Pour exact + glissant à la minute, on stockerait 7*24 clichés horaires.)
      static uint8_t daysSinceWeekBase = 0;
      daysSinceWeekBase = (daysSinceWeekBase + 1) % 7;
      if (daysSinceWeekBase == 0) {
        g_rainCum_t0w = g_rainCum_now; // rebase toutes les 7 * 24 h
      }
    }

    
    // Anémomètre
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

    
    if(old_vitesse != vitesse){
      //updateAnemometre(1, vitesse); // Met à jour la valeur de l'anémomètre dans la base de données
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
     
    


    Serial.print("DateTime = ");
    Serial.println(datetime);

    // Envoi des données à l'API si activé
   

  
  auto resched = [](unsigned long &dueAt, unsigned long period, unsigned long jitter=0){
    dueAt += period;                                   // période stable
    if ((long)(millis() - dueAt) >= 0) dueAt = millis() + period; // rattrapage si retard
    if (jitter) dueAt += jitter;                       // petit décalage
  };


    
  // --- Lectures capteurs périodiques & mise à jour DB (toutes les 30 s)
  unsigned long nowMs = millis();
  if (timeReached(nowMs, nextUpdateMs)) {
    Serial.println("--- Mise à jour des données ---");

    // Anémomètre
    if (module_anemo == 1) {
      etat_anemo = anemo_ok_bit_strict();
      anemo_update();
      vitesse = anemo_get_speed_kmh();
      rafale  = anemo_get_gust_kmh();
      // Debug optionnel
      // Serial.printf("Anémo: %.1f km/h, Rafale: %.1f km/h\n", vitesse, rafale);
    } else {
      etat_anemo = 0; vitesse = 0; rafale = 0;
    }

    // --- BMP280 avec sanity-check & auto-réinit douce ---
    if (module_bmp280 == 1) {
      if (bmp_ok) {
        readBMP280(temp1, pression, altitude);
        noteI2C_OK();  // marquer un succès I2C
      } else {
        bmp_ok = initBMP280();
      }

      if (bmp_ok && saneBMP(temp1, pression)) {
        if (bmpBadStreak != 0) {
          app_logf("[BMP280] Lecture redevenue valide après %u erreurs", bmpBadStreak);
        }
        bmpBadStreak = 0;
        etat_bmp280 = BMP_OK;

        float temp1_corr = temp1 + ofs.t_bmp;
        float press_corr = pression + ofs.press;
        g_snap.temp_bmp  = temp1_corr;
        g_snap.press_hPa = press_corr;
      } else {
        bmpBadStreak++;
        etat_bmp280 = bmp_ok ? BMP_NAN : BMP_BUS_BAD;

        temp1 = NAN;
        pression = NAN;
        altitude = NAN;
        g_snap.temp_bmp = NAN;
        g_snap.press_hPa = NAN;

        if (bmpBadStreak == 1) {
          app_logf("[BMP280] Lecture invalide t=%.2fC p=%.2fhPa (streak=%u) — ignore",
                  temp1, pression, bmpBadStreak);
        }

        if (bmpBadStreak >= 2) {
          etat_bmp280 = BMP_RESETTING;
          bool sr = bmp_soft_reset();
          bmp_ok = initBMP280();
          app_logf("[BMP280] Soft reset %s, re-init -> %s",
                  sr ? "OK" : "KO", bmp_ok ? "OK" : "KO");
          if (bmp_ok) {
            bmpBadStreak = 0;
          }
        }

        if (!bmp_ok && bmpBadStreak >= 4) {
          module_bmp280 = 0;
          app_logf("[BMP280] Désactivé après plusieurs erreurs");
        }
      }
    } else {
      etat_bmp280 = 0;
      temp1 = pression = altitude = NAN;
      g_snap.temp_bmp = NAN;
      g_snap.press_hPa = NAN;
    }

    //Reinitialisation douce du BMP280 si demandé via l'API (bmpReinitReq)
    if (bmpReinitReq) {
      bmpReinitReq = false;
      etat_bmp280 = BMP_RESETTING;
      bool sr = bmp_soft_reset();               // helpers de ta lib
      bmp_ok = initBMP280();
      app_logf("[BMP280] Soft reset %s, re-init -> %s",
              sr ? "OK" : "KO", bmp_ok ? "OK" : "KO");
    }


    // DHT22 (lire seulement si activé et prêt)
    if (module_dht22 == 1 && isDHTReady()) {
      temp2     = getTemperature();
      humiditer = getHumidity();
      etat_dht22 = (temp2 != -999.0 && humiditer != -999.0) ? 1 : 0;
    } else {
      etat_dht22 = 0; temp2 = 0; humiditer = 0;
    }

    // SHT40 (si activé)
    if (module_sht40 == 1) {
      float tS = getSHT40Temperature();
      float hS = getSHT40Humidity();
      if (tS == -999.0 || hS == -999.0) {
        Serial.println("Erreur lecture SHT40");
        app_logf("[SHT40] Erreur lecture SHT40");
      }
    }

    // Tensions
    tension_solaire  = solaire();
    tension_batterie = batterie();

    // Pluviomètre
    if (module_pluvio == 1) {
      gestionPluviometre();
      etat_pluvio = pluvio_active_bit();
      quantite    = obtenirQuantitePluie_mm();
    } else {
      etat_pluvio = 0; quantite = 0;
    }

    // Girouette
    if (module_girou == 1) {
      girouette.update(/*debounceMs=*/2, /*stableReads=*/3);
      handleCalibrationButton();
      float angle = girouette.readAngle(30);
      degret = isnan(angle) ? -1 : (int)angle;
      etat_girou = (degret >= 0) ? 1 : 0;
    } else {
      degret = 0; etat_girou = 0;
    }

    
  // --- Appliquer les offsets aux mesures brutes ---
  float temp1_corr = temp1 + ofs.t_bmp;         // °C BMP
  float temp2_corr = temp2 + ofs.t_dht;         // °C DHT
  float hum_corr   = ((float)humiditer) + ofs.h_dht;
  if (hum_corr < 0) hum_corr = 0; if (hum_corr > 100) hum_corr = 100;
  int   humiditer_corr = (int)roundf(hum_corr);

  float press_corr  = pression + ofs.press;     // hPa
  float vent_corr   = max(0.0f, vitesse + ofs.wind);
  float rafale_corr = max(0.0f, rafale  + ofs.wind);

  // Point de rosée avec valeurs corrigées
  // Point de rosée (à partir des mesures du moment)
  float point_de_rosee = calculPointRosee(temp1_corr, humiditer_corr);

  // --- Stats (journalières + all-time) sur valeurs corrigées ---
  time_t nowEpoch = now.unixtime();
  updateStat(ST_tempExt, temp1_corr, nowEpoch);
  updateStat(ST_humExt,  (float)humiditer_corr, nowEpoch);
  updateStat(ST_press,   press_corr,  nowEpoch);

  
  // --- Snapshot RAM pour l'UI / JSON (corrigé) ---
  //g_snap.temp_bmp  = temp1_corr;
  //g_snap.hum       = (float)humiditer_corr;
  g_snap.press_hPa = press_corr;
  g_snap.wind      = vent_corr;
  g_snap.gust      = rafale_corr;
  g_snap.dir_deg   = degret;
  g_snap.rain_cum  = quantite;
  g_snap.batt_v    = tension_batterie;
  g_snap.solar_v   = tension_solaire;
  g_snap.dew       = point_de_rosee;
  strncpy(g_snap.datetime, datetime, sizeof(g_snap.datetime)-1);
  g_snap.datetime[sizeof(g_snap.datetime)-1] = '\0';
  g_snap.now_ms    = nowMs;

    
    

    // --- Mise à jour DB (station_direct / tensions / état capteurs)
    if (!otaInProgress) {
      Serial.println("Mise à jour de la base de données.");
      float temp1_r         = round2f(temp1_corr);
      float pression_r      = round2f(press_corr);
      float temp2_r         = round2f(temp2_corr);
      float humiditer_r     = round2f(humiditer_corr);
      float rosee_r         = round2f(point_de_rosee);
      float vitesse_r       = round2f(vent_corr);
      float rafale_r        = round2f(rafale_corr);
      float vb_r            = round2f(tension_batterie);
      float vs_r            = round2f(tension_solaire);

      
    }

    // --- Stats & records (journaliers + all-time)
    updateStat(ST_tempExt, temp1_corr, nowEpoch);
    updateStat(ST_humExt,  (float)humiditer_corr, nowEpoch);
    updateStat(ST_press,   press_corr,  nowEpoch);
    updateStat(ST_batt,    tension_batterie, nowEpoch);
    updateStat(ST_solar,   tension_solaire,  nowEpoch);

    if (!isnan(rafale_corr)) {
      if (rafale_corr > GUST_maxDay) { GUST_maxDay = rafale_corr; GUST_dirDay = degret; }
      if (rafale_corr > GUST_maxAll) { GUST_maxAll = rafale_corr; GUST_dirAll = degret; g_recordsDirty = true; }
    }
    saveAllTimeRecordsIfDirty();

    // --- PLANIFICATION des prochains push API par rapport à CETTE mise à jour
    //     (on décale l’API pour laisser le temps aux lectures et DB de se stabiliser)
    reschedStable(nextUpdateMs, UPDATE_PERIOD_MS);     // replanifier la prochaine mise à jour capteurs
    
    Serial.printf("[30s] nextUpdateMs=%lu nextDueRow=%lu nextDueEtat=%lu\n",
                  nextUpdateMs, nextDueRow, nextDueEtat);
  
  
      // --- NO-DB: remplir le snapshot RAM
      g_snap.temp_dht = temp2_corr;                     // DHT22
      g_snap.hum = (float)humiditer_corr;               // DHT22
      
      // NE publier BMP que si etat_bmp280 == BMP_OK
   
    if (etat_bmp280 == BMP_OK) {

        if (!isnan(temp1_corr) && temp1_corr > -45.0f && temp1_corr < 85.0f) {
            g_snap.temp_bmp = temp1_corr;
        }

        if (!isnan(press_corr) && press_corr > 300.0f && press_corr < 1100.0f) {
            g_snap.press_hPa = press_corr;
        }
    }


      g_snap.wind = vent_corr;
      g_snap.gust = rafale_corr;
      g_snap.dir_deg = degret;
      g_snap.rain_cum = quantite;

      g_snap.batt_v = tension_batterie;
      g_snap.solar_v = tension_solaire;
      g_snap.dew = point_de_rosee;

      strncpy(g_snap.datetime, datetime, sizeof(g_snap.datetime)-1);
      g_snap.datetime[sizeof(g_snap.datetime)-1] = '\0';
      g_snap.now_ms = nowMs;
      
    // --- Compteur monotone (1 par cycle 30 s)
    compteur++;

    // Sauvegarde NVS toutes les 10 minutes (anti-usure flash)
    static unsigned long tCtrSave = 0;
    if (millis() - tCtrSave >= 600000UL) { // 600000 ms = 10 min
      saveCounterToNvs();
      tCtrSave = millis();
    }

    if (firstRun && activation_envoi_api == 1) {

        if (!g_staConnected) {
          // on attend juste le WiFi
          Serial.println("⏳ Attente WiFi pour envoi initial...");
        } 
        else {

          Serial.println("🚀 Envoi API au démarrage");

          pushBusy = true;

          sendLatestRowToApi();
          sendLatestEtatStationMeteoToApi();

          pushBusy = false;

          firstRun = false;

          // ✅ IMPORTANT : replanifier le cycle
          unsigned long nowMs = millis();
          nextDueRow  = nowMs + ROW_PERIOD_MS;
          nextDueEtat = nowMs + ETAT_PERIOD_MS;
        }
      }

 }

  // --- Envoi API (indépendant, cadencé par nextDueRow/nextDueEtat)
  
  bool dueRow  = ((long)(nowMs - nextDueRow)  >= 0);
  bool dueEtat = ((long)(nowMs - nextDueEtat) >= 0);
  
  if (dueRow)  log_line("API: envoi station_direct");
  if (dueEtat) log_line("API: envoi etatcapteurs");


  Serial.printf("[SCHED] act=%d busy=%d now=%lu dueRow=%lu (in %ld ms) dueEtat=%lu (in %ld ms)\n",
                activation_envoi_api, pushBusy, nowMs,
                nextDueRow,  (long)(nextDueRow  - nowMs),
                nextDueEtat, (long)(nextDueEtat - nowMs));

  
  // --- Garder l'horaire propre même si on n'envoie pas maintenant ---


  // Maintenir les échéances dans le FUTUR quand on n'envoie pas
  if (activation_envoi_api != 1 || otaInProgress || pushBusy) {
    if ((long)(nowMs - nextDueRow)  >= 0)  { reschedForwardToFuture(nextDueRow,  ROW_PERIOD_MS); }
    if ((long)(nowMs - nextDueEtat) >= 0)  { reschedForwardToFuture(nextDueEtat, ETAT_PERIOD_MS); }
  }



if (activation_envoi_api == 1 && !otaInProgress && !pushBusy) {

    if (!g_staConnected) {
        Serial.println("⚠️ WiFi pas prêt -> skip API");
    } 
    else {

        pushBusy = true;

        // ✅ station_direct toutes les 30s
        if ((long)(nowMs - nextDueRow) >= 0) {

            Serial.println("🚀 Envoi station_direct");

            sendLatestRowToApi();

            // ✅ avancer le timer
            nextDueRow += ROW_PERIOD_MS;
        }

        // ✅ etat_station toutes les 60s
        if ((long)(nowMs - nextDueEtat) >= 0) {

            Serial.println("📡 Envoi etat_station");

            sendLatestEtatStationMeteoToApi();

            // ✅ avancer le timer
            nextDueEtat += ETAT_PERIOD_MS;
        }

        pushBusy = false;
    }

}
else if (activation_envoi_api != 1) {

    static unsigned long tLastApiOffLog = 0;

    if (millis() - tLastApiOffLog >= 60000UL) {
        Serial.println(F("Envoi des données à l'API désactivé."));
        log_line("API: Envoi des données à l'API désactivé.");
        tLastApiOffLog = millis();
    }
}

  log_tick();
      // ---- Watchdog I²C : supervision non agressive ----
{
  const unsigned long now = millis();

  // Conditions d'inhibition : disabled, OTA, période de grâce
  if (!g_i2cWdEnabled || otaInProgress || now < I2C_WD_GRACE_MS) {
    // ne rien faire
  } else {
    // Pas de succès I²C depuis trop longtemps ?
    if ((now - g_lastI2CokMs) > I2C_WATCHDOG_MS && (now - g_lastI2CcheckMs) > 500UL) {
      g_lastI2CcheckMs = now;
      app_logf("[I2C] Aucun succès I2C depuis %lu ms -> tentative recovery",
               (unsigned long)(now - g_lastI2CokMs));

      bool ok = i2c_recover_sequence();
      if (ok) {
        noteI2C_OK();
      } else {
        g_i2cRecoverAttempts++;

        // IMPORTANT : si on n’a JAMAIS eu de succès depuis le boot, on n’ira pas au reboot
        const bool neverOK = (g_lastI2CokMs == 0);

        app_logf("[I2C] Recovery KO (tentative %d/%d, neverOK=%d)",
                 g_i2cRecoverAttempts, I2C_MAX_RECOVER, neverOK);

        if (!neverOK && g_i2cRecoverAttempts >= I2C_MAX_RECOVER) {
          app_logf("[I2C] Trop d'échecs -> reboot ESP32");
          g_i2cWdReboots++;
          delay(150);
          ESP.restart();
        }
      }
    }
  }
}
  delay(100);
}


  


