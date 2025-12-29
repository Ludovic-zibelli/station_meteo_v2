#include <Arduino.h>
#include <sqlite3.h>
#include "db_read.h"
#include "bd_mgr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// db est ouverte ailleurs
extern sqlite3 *db;

static void fillFromStmt(sqlite3_stmt *stmt, StationDirect &o) {
  o.id            = sqlite3_column_int(stmt, 0);
  o.tempdht22     = sqlite3_column_double(stmt, 1);
  o.humiditer     = sqlite3_column_double(stmt, 2);
  o.tempbmp280    = sqlite3_column_double(stmt, 3);
  o.pression      = sqlite3_column_double(stmt, 4);
  o.lumiere       = sqlite3_column_double(stmt, 5);
  o.anemometre    = sqlite3_column_double(stmt, 6);
  o.girouette     = sqlite3_column_double(stmt, 7);
  o.pluviometre   = sqlite3_column_double(stmt, 8);
  o.pointderosee  = sqlite3_column_double(stmt, 9);
  o.ghost         = sqlite3_column_double(stmt,10);
  o.tpsvie        = sqlite3_column_double(stmt,11);
  const unsigned char *ts = sqlite3_column_text(stmt, 12);
  o.timestamp = ts ? String((const char*)ts) : String("");
  o.rafale        = sqlite3_column_double(stmt,13);
}

bool readStationDirectById(int id, StationDirect &out) {
  const char *sql =
    "SELECT id,tempdht22,humiditer,tempbmp280,pression,lumiere,"
    "anemometre,girouette,pluviometre,pointderosee,ghost,tpsvie,timestamp,rafale "
    "FROM station_direct WHERE id=?;";
  sqlite3_stmt *stmt = nullptr;
  if (g_dbMutex) xSemaphoreTake(g_dbMutex, pdMS_TO_TICKS(2000));
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    if (g_dbMutex) xSemaphoreGive(g_dbMutex);
    return false;
  }
  sqlite3_bind_int(stmt, 1, id);
  bool ok = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) { fillFromStmt(stmt, out); ok = true; }
  sqlite3_finalize(stmt);
  if (g_dbMutex) xSemaphoreGive(g_dbMutex);
  return ok;
}

bool readLatestStationDirect(StationDirect &out) {
  const char *sql =
    "SELECT id,tempdht22,humiditer,tempbmp280,pression,lumiere,"
    "anemometre,girouette,pluviometre,pointderosee,ghost,tpsvie,timestamp,rafale "
    "FROM station_direct "
    "ORDER BY datetime(timestamp) DESC, id DESC LIMIT 1;";
  sqlite3_stmt *stmt = nullptr;
  if (g_dbMutex) xSemaphoreTake(g_dbMutex, pdMS_TO_TICKS(2000));
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    if (g_dbMutex) xSemaphoreGive(g_dbMutex);
    return false;
  }
  bool ok = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) { fillFromStmt(stmt, out); ok = true; }
  sqlite3_finalize(stmt);
  if (g_dbMutex) xSemaphoreGive(g_dbMutex);
  return ok;
}

bool readModulesById(int id,
                     int &module_bmp280, int &module_dht22, int &module_sht40,
                     int &module_anemo, int &module_girou, int &module_pluvio,
                     int &module_tension, int &module_bitvie) {
  const char *sql =
    "SELECT module_bmp280, module_dht22, module_sht40, module_anemo, "
    "module_girou, module_pluvio, module_tension, module_bitvie "
    "FROM etatcapteurs WHERE id=?;";

  sqlite3_stmt *stmt = nullptr;
  if (g_dbMutex) xSemaphoreTake(g_dbMutex, pdMS_TO_TICKS(2000));
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    if (g_dbMutex) xSemaphoreGive(g_dbMutex);
    return false;
  }
  sqlite3_bind_int(stmt, 1, id);

  bool ok = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    module_bmp280  = sqlite3_column_int(stmt, 0);
    module_dht22   = sqlite3_column_int(stmt, 1);
    module_sht40   = sqlite3_column_int(stmt, 2);
    module_anemo   = sqlite3_column_int(stmt, 3);
    module_girou   = sqlite3_column_int(stmt, 4);
    module_pluvio  = sqlite3_column_int(stmt, 5);
    module_tension = sqlite3_column_int(stmt, 6);
    module_bitvie  = sqlite3_column_int(stmt, 7);
    ok = true;
  }

  sqlite3_finalize(stmt);
  if (g_dbMutex) xSemaphoreGive(g_dbMutex);
  return ok;
}

bool updateModuleVariablesFromDB(int id) {
  const char *sql =
    "SELECT module_bmp280, module_dht22, module_anemo, module_girou, "
    "module_pluvio, module_tension, module_bitvie, module_sht40 "
    "FROM etatcapteurs WHERE id=?;";

  sqlite3_stmt *stmt = nullptr;
  if (g_dbMutex) xSemaphoreTake(g_dbMutex, pdMS_TO_TICKS(2000));
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    if (g_dbMutex) xSemaphoreGive(g_dbMutex);
    return false;
  }
  sqlite3_bind_int(stmt, 1, id);

  bool ok = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    module_bmp280  = sqlite3_column_int(stmt, 0);
    module_dht22   = sqlite3_column_int(stmt, 1);
    module_anemo   = sqlite3_column_int(stmt, 2);
    module_girou   = sqlite3_column_int(stmt, 3);
    module_pluvio  = sqlite3_column_int(stmt, 4);
    module_tension = sqlite3_column_int(stmt, 5);
    module_bitvie  = sqlite3_column_int(stmt, 6);
    module_sht40  = sqlite3_column_int(stmt, 7);
    ok = true;
  }

  sqlite3_finalize(stmt);
  if (g_dbMutex) xSemaphoreGive(g_dbMutex);
  return ok;
}




bool readActivationApi(int &activation) {
    const char *sql = "SELECT activation_envoi_api FROM config ORDER BY id DESC LIMIT 1;";
    sqlite3_stmt *stmt = nullptr;
    if (g_dbMutex) xSemaphoreTake(g_dbMutex, pdMS_TO_TICKS(2000));

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      Serial.printf("❌ Prepare error (activation_envoi_api): %s\n", sqlite3_errmsg(db));
      if (g_dbMutex) xSemaphoreGive(g_dbMutex);
      return false;
    }

    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        activation = sqlite3_column_int(stmt, 0);
        ok = true;
    } else {
        Serial.printf("❌ Step error (activation_envoi_api): %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    if (g_dbMutex) xSemaphoreGive(g_dbMutex);
    return ok;
}

bool readLatestEtatCapteurs(EtatCapteurs& out) {
    const char *sql =
        "SELECT id, module_bmp280, module_dht22, module_anemo, module_girou, module_pluvio, module_tension, module_bitvie, "
        "capteur_dht22, capteur_bmp280, capteur_pluvio, capteur_girou, capteur_anemo, "
        "log_date_bmp280, log_bmp280, log_date_dht22, log_dht22, log_date_girou, log_girou, "
        "log_date_tension, log_tension, log_date_anemo, log_anemo, log_date_pluvio, log_pluvio, "
        "tension_solaire, tension_batterie "
        "FROM etatcapteurs ORDER BY id DESC LIMIT 1;";
    sqlite3_stmt *stmt = nullptr;
    if (g_dbMutex) xSemaphoreTake(g_dbMutex, pdMS_TO_TICKS(2000));
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      if (g_dbMutex) xSemaphoreGive(g_dbMutex);
      return false;
    }
    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.id                = sqlite3_column_int(stmt, 0);
        out.module_bmp280     = sqlite3_column_int(stmt, 1);
        out.module_dht22      = sqlite3_column_int(stmt, 2);
        out.module_anemo      = sqlite3_column_int(stmt, 3);
        out.module_girou      = sqlite3_column_int(stmt, 4);
        out.module_pluvio     = sqlite3_column_int(stmt, 5);
        out.module_tension    = sqlite3_column_int(stmt, 6);
        out.module_bitvie     = sqlite3_column_int(stmt, 7);
        out.capteur_dht22     = sqlite3_column_int(stmt, 8);
        out.capteur_bmp280    = sqlite3_column_int(stmt, 9);
        out.capteur_pluvio    = sqlite3_column_int(stmt, 10);
        out.capteur_girou     = sqlite3_column_int(stmt, 11);
        out.capteur_anemo     = sqlite3_column_int(stmt, 12);
        out.log_date_bmp280   = (const char*)sqlite3_column_text(stmt, 13);
        out.log_bmp280        = (const char*)sqlite3_column_text(stmt, 14);
        out.log_date_dht22    = (const char*)sqlite3_column_text(stmt, 15);
        out.log_dht22         = (const char*)sqlite3_column_text(stmt, 16);
        out.log_date_girou    = (const char*)sqlite3_column_text(stmt, 17);
        out.log_girou         = (const char*)sqlite3_column_text(stmt, 18);
        out.log_date_tension  = (const char*)sqlite3_column_text(stmt, 19);
        out.log_tension       = (const char*)sqlite3_column_text(stmt, 20);
        out.log_date_anemo    = (const char*)sqlite3_column_text(stmt, 21);
        out.log_anemo         = (const char*)sqlite3_column_text(stmt, 22);
        out.log_date_pluvio   = (const char*)sqlite3_column_text(stmt, 23);
        out.log_pluvio        = (const char*)sqlite3_column_text(stmt, 24);
        out.tension_solaire   = sqlite3_column_double(stmt, 25);
        out.tension_batterie  = sqlite3_column_double(stmt, 26);
        ok = true;
    }
    sqlite3_finalize(stmt);
    if (g_dbMutex) xSemaphoreGive(g_dbMutex);
    return ok;
}


bool readAppConfig(AppConfig& c) {
  const char* sql =
    "SELECT ssid_wifi, pass_wifi, IP_WIFI, ID_STATION, adresse_api, token, activation_envoi_api "
    "FROM config WHERE id=1 LIMIT 1;";
  sqlite3_stmt* stmt = nullptr;
  if (g_dbMutex) xSemaphoreTake(g_dbMutex, pdMS_TO_TICKS(2000));
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    if (g_dbMutex) xSemaphoreGive(g_dbMutex);
    return false;
  }
  bool ok = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    c.ssid_wifi  = (const char*)sqlite3_column_text(stmt, 0);
    c.pass_wifi  = (const char*)sqlite3_column_text(stmt, 1);
    c.ip_wifi    = (const char*)sqlite3_column_text(stmt, 2);
    c.id_station = (const char*)sqlite3_column_text(stmt, 3);
    c.adresse_api= (const char*)sqlite3_column_text(stmt, 4);
    c.token      = (const char*)sqlite3_column_text(stmt, 5);
    c.activation_envoi_api = sqlite3_column_int(stmt, 6);
    ok = true;
  }
  sqlite3_finalize(stmt);
  if (g_dbMutex) xSemaphoreGive(g_dbMutex);
  return ok;
}
