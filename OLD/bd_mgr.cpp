// --- db_mgr.cpp ---
#include "bd_mgr.h"
#include "log.h"
#include "db_read.h"   // readAppConfig(...)
#include "bd.h"        // updateAppConfig(...)
#include <cstring>
#include <Preferences.h>
#include <FS.h>
#include <LittleFS.h>

static Preferences prefsCfgBackup;


static size_t spiffs_free_bytes() {
  return LittleFS.totalBytes() - LittleFS.usedBytes();
}

// bd_mgr.cpp
static bool open_db(const char* path) {
  int rc = sqlite3_open(path, &db);
  if (rc != SQLITE_OK) { db = nullptr; return false; }

  // PRAGMAs adaptés au flash :
  sqlite3_exec(db, "PRAGMA journal_mode=OFF;",   nullptr, nullptr, nullptr);
  sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr); // NORMAL = compromis sûreté/coût
  sqlite3_exec(db, "PRAGMA temp_store=MEMORY;",  nullptr, nullptr, nullptr);
  sqlite3_exec(db, "PRAGMA locking_mode=EXCLUSIVE;", nullptr, nullptr, nullptr);
  // Cache modéré + délai de busy
  sqlite3_exec(db, "PRAGMA cache_size=-1024;",   nullptr, nullptr, nullptr); // ~1 Mo
  sqlite3_busy_timeout(db, 250);
  return true;
}

sqlite3* db = nullptr;
SemaphoreHandle_t g_dbMutex = nullptr;

// Déclare la variable globale définie dans main.cpp
extern volatile bool otaInProgress;


// --- Helpers d'intégrité/réparation ---
static const char* DB_PATH = "littlefs/station.db";



bool fileLooksLikeSQLite(const char* path) {
  if (!path) path = "/littlefs/station.db";  // sécurité si NULL
  if (!LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  char hdr[16] = {0};
  size_t n = f.readBytes(hdr, 16);
  f.close();
  if (n < 16) return false;
  const char sig[] = "SQLite format 3";
  return memcmp(hdr, sig, strlen(sig)) == 0;
}


bool dbIntegrityCheck() {
  // Temporairement désactivé pour éviter abort
  return true;
}

bool createSchemaAndSeed(sqlite3* db) {
  const char* DDL[] = {
    "CREATE TABLE IF NOT EXISTS station_direct ("
      "id INTEGER PRIMARY KEY,"
      "tempdht22 REAL, humiditer REAL, tempbmp280 REAL, pression REAL, lumiere REAL,"
      "anemometre REAL, girouette REAL, pluviometre REAL, pointderosee REAL, ghost REAL,"
      "tpsvie REAL, timestamp TEXT, rafale REAL"
    ");",
    "CREATE TABLE IF NOT EXISTS tensions ("
      "id INTEGER PRIMARY KEY, tension_batterie REAL, tension_solaire REAL"
    ");",
    "CREATE TABLE IF NOT EXISTS etatcapteurs ("
      "id INTEGER PRIMARY KEY,"
      "module_bmp280 INTEGER, module_dht22 INTEGER, module_anemo INTEGER, module_girou INTEGER, module_pluvio INTEGER, module_tension INTEGER, module_bitvie INTEGER, module_sht40 INTEGER,"
      "capteur_dht22 INTEGER, capteur_bmp280 INTEGER, capteur_pluvio INTEGER, capteur_girou INTEGER, capteur_anemo INTEGER,"
      "log_date_bmp280 TEXT, log_bmp280 TEXT, log_date_dht22 TEXT, log_dht22 TEXT, log_date_girou TEXT, log_girou TEXT,"
      "log_date_tension TEXT, log_tension TEXT, log_date_anemo TEXT, log_anemo TEXT, log_date_pluvio TEXT, log_pluvio TEXT,"
      "tension_solaire REAL, tension_batterie REAL"
    ");",
    "CREATE TABLE IF NOT EXISTS config ("
      "id INTEGER PRIMARY KEY,"
      "ssid_wifi TEXT, pass_wifi TEXT, IP_WIFI TEXT, ID_STATION TEXT, adresse_api TEXT, token TEXT, activation_envoi_api INTEGER"
    ");",
    "PRAGMA journal_mode=OFF;",
    "PRAGMA synchronous=NORMAL;",
    "PRAGMA temp_store=MEMORY;",
    "PRAGMA locking_mode=EXCLUSIVE;"
  };
  for (auto sql : DDL) {
    char* err=nullptr; int rc=sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (rc!=SQLITE_OK) { if (err) sqlite3_free(err); return false; }
    if (err) sqlite3_free(err);
  }
  const char* seed[] = {
    "INSERT OR IGNORE INTO station_direct(id,tempdht22,humiditer,tempbmp280,pression,lumiere,anemometre,girouette,pluviometre,pointderosee,ghost,tpsvie,timestamp,rafale)"
    "VALUES(1,0,0,0,0,0,0,0,0,0,0,0,'1970-01-01 00:00:00',0);",
    "INSERT OR IGNORE INTO tensions(id,tension_batterie,tension_solaire) VALUES(1,0,0);",
    "INSERT OR IGNORE INTO etatcapteurs("
    "id,module_bmp280,module_dht22,module_anemo,module_girou,module_pluvio,module_tension,module_bitvie,module_sht40,"
    "capteur_dht22,capteur_bmp280,capteur_pluvio,capteur_girou,capteur_anemo,"
    "log_date_bmp280,log_bmp280,log_date_dht22,log_dht22,log_date_girou,log_girou,"
    "log_date_tension,log_tension,log_date_anemo,log_anemo,log_date_pluvio,log_pluvio,"
    "tension_solaire,tension_batterie)"
    "VALUES(1,1,1,1,1,1,1,0, 0,0,0,0,0, NULL,NULL,NULL,NULL,NULL,NULL, NULL,NULL,NULL,NULL,NULL,NULL, 0,0);",
    "INSERT OR IGNORE INTO config(id,ssid_wifi,pass_wifi,IP_WIFI,ID_STATION,adresse_api,token,activation_envoi_api)"
    "VALUES(1,'','','dhcp','1','','',1);"
  };
  for (auto sql : seed) {
    char* err=nullptr; int rc=sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (rc!=SQLITE_OK) { if (err) sqlite3_free(err); return false; }
    if (err) sqlite3_free(err);
  }
  return true;
}



bool recreateDatabaseFile() {
  // 1) Sauvegarde de config (DB si lisible)
  AppConfig backup;
  bool haveCfg = db && readAppConfig(backup);

  // 2) Renommer l'ancienne DB
  if (db) { sqlite3_close(db); db=nullptr; }
  const String bak = String(DB_PATH) + ".bad";
  if (LittleFS.exists(bak)) LittleFS.remove(bak);
  if (LittleFS.exists(DB_PATH)) LittleFS.rename(DB_PATH, bak);

  // 3) Recréer la DB + schéma
  int rc = sqlite3_open(DB_PATH, &db);
  if (rc != SQLITE_OK) { db=nullptr; return false; }
  sqlite3_exec(db,"PRAGMA journal_mode=OFF;",nullptr,nullptr,nullptr);
  sqlite3_exec(db,"PRAGMA synchronous=NORMAL;",nullptr,nullptr,nullptr);
  sqlite3_exec(db,"PRAGMA temp_store=MEMORY;",nullptr,nullptr,nullptr);
  sqlite3_exec(db,"PRAGMA locking_mode=EXCLUSIVE;",nullptr,nullptr,nullptr);
  if (!createSchemaAndSeed(db)) return false;

  // 4) Restaurer config
  if (haveCfg) {
    updateAppConfig(backup);            // DB -> DB
  } else {
    // 🔁 Fallback NVS si tu as mis en place le backup NVS :
    AppConfig fromNvs;
    if (loadConfigFromNVS(fromNvs)) {
      updateAppConfig(fromNvs);         // NVS -> DB
    }
  }

  // 5) Vérification intégrité
  bool ok = dbIntegrityCheck();

  // 6) Politique de backup:
  //    - Par défaut: supprimer .bad si tout est OK.
  //    - Sinon: garder seulement si on a beaucoup de marge.
  if (ok) {
    // Supprimer le backup pour ne pas saturer LittleFS
    if (LittleFS.exists(bak)) {
      LittleFS.remove(bak);
      app_logf("[DB] recreate OK -> removed backup .bad");
    }
  } else {
    app_logf("[DB] recreate FAILED -> keeping backup .bad");
    // Fermer la DB corrompue pour éviter les crashes
    sqlite3_close(db);
    db = nullptr;
  }

  // Variante "garder seulement si free >= seuil":
  // size_t freeB = spiffs_free_bytes();
  // const size_t MIN_KEEP_FREE = 150*1024; // ex. 150 Ko
  // if (ok && freeB < MIN_KEEP_FREE && SPIFFS.exists(bak)) {
  //   SPIFFS.remove(bak);
  //   app_logf("[DB] low free -> removed backup .bad (free=%u)", (unsigned)freeB);
  // }

  return ok;
}


// À appeler si erreur 26 remontée (NOTADB)
bool db_try_repair_if_notadb() {
  // protégé par g_dbMutex depuis l'appelant
  if (!fileLooksLikeSQLite(DB_PATH)) return recreateDatabaseFile();
  if (!dbIntegrityCheck())           return recreateDatabaseFile();
  return false;
}


bool db_reopen_if_needed(const char* path) {
  if (otaInProgress) {
    Serial.println("db_reopen_if_needed: OTA in progress, skipping reopen");
    app_logf("db_reopen_if_needed: OTA in progress, skipping reopen");
    return false;
  }
  // 🔒 mêmes règles d'accès que le reste
  if (g_dbMutex) xSemaphoreTake(g_dbMutex, pdMS_TO_TICKS(200));

  bool need = (!db) || (db && sqlite3_errcode(db) != SQLITE_OK);

  if (need) {
    if (db) { sqlite3_close(db); db=nullptr; }
    bool ok = open_db(path);
    if (g_dbMutex) xSemaphoreGive(g_dbMutex);
    return ok;
  }

  if (g_dbMutex) xSemaphoreGive(g_dbMutex);
  return true;
}


bool db_begin() {
  // Ne pas (ré)ouvrir la LittleFS/DB pendant une OTA
  if (otaInProgress) {
    Serial.println("db_begin: OTA in progress, skipping DB open");
    app_logf("db_begin: OTA in progress, skipping DB open");
    return false;
  }

  if (!LittleFS.begin()) return false;
  if (!open_db(DB_PATH)) return false;
  if (!g_dbMutex) g_dbMutex = xSemaphoreCreateMutex();
  return g_dbMutex != nullptr;
}

void db_end() {
  if (db) { sqlite3_close(db); db = nullptr; }
}

bool db_exec(const char* sql) {
  if (!db) return false;
  char* err = nullptr;
  int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    if (err) { Serial.printf("SQL error: %s\n", err); sqlite3_free(err); }
    return false;
  }
  return true;
}

//Sauvegarde de la config dans la NVS
void backupConfigToNVS(const AppConfig& c) {
  prefsCfgBackup.begin("cfgbak", /*rw=*/false);
  prefsCfgBackup.putString("ssid",  c.ssid_wifi);
  prefsCfgBackup.putString("pass",  c.pass_wifi);
  prefsCfgBackup.putString("ip",    c.ip_wifi);
  prefsCfgBackup.putString("id",    c.id_station);
  prefsCfgBackup.putString("url",   c.adresse_api);
  prefsCfgBackup.putString("token", c.token);
  prefsCfgBackup.putInt   ("envoi", c.activation_envoi_api);
  prefsCfgBackup.end();
}

bool loadConfigFromNVS(AppConfig& c) {
  prefsCfgBackup.begin("cfgbak", /*ro=*/true);
  String ssid  = prefsCfgBackup.getString("ssid", "");
  String pass  = prefsCfgBackup.getString("pass", "");
  if (ssid == "" /* pas de sauvegarde */) { prefsCfgBackup.end(); return false; }
  c.ssid_wifi  = ssid;
  c.pass_wifi  = pass;
  c.ip_wifi    = prefsCfgBackup.getString("ip", "dhcp");
  c.id_station = prefsCfgBackup.getString("id", "1");
  c.adresse_api= prefsCfgBackup.getString("url", "");
  c.token      = prefsCfgBackup.getString("token", "");
  c.activation_envoi_api = prefsCfgBackup.getInt("envoi", 1);
  prefsCfgBackup.end();
  return true;
}

