#include <Arduino.h>
#include <sqlite3.h>
#include "db_read.h"

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
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
  sqlite3_bind_int(stmt, 1, id);
  bool ok = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) { fillFromStmt(stmt, out); ok = true; }
  sqlite3_finalize(stmt);
  return ok;
}

bool readLatestStationDirect(StationDirect &out) {
  const char *sql =
    "SELECT id,tempdht22,humiditer,tempbmp280,pression,lumiere,"
    "anemometre,girouette,pluviometre,pointderosee,ghost,tpsvie,timestamp,rafale "
    "FROM station_direct "
    "ORDER BY datetime(timestamp) DESC, id DESC LIMIT 1;";
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
  bool ok = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) { fillFromStmt(stmt, out); ok = true; }
  sqlite3_finalize(stmt);
  return ok;
}
