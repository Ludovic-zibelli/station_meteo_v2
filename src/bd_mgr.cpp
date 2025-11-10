// --- db_mgr.cpp ---
#include "bd_mgr.h"
sqlite3* db = nullptr;
SemaphoreHandle_t g_dbMutex = nullptr;

static bool open_db(const char* path) {
  int rc = sqlite3_open(path, &db);
  if (rc != SQLITE_OK) { db = nullptr; return false; }
  // Quelques PRAGMA raisonnables sur ESP32 + SPIFFS
  sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
  sqlite3_exec(db, "PRAGMA temp_store=MEMORY;", nullptr, nullptr, nullptr);
  sqlite3_exec(db, "PRAGMA cache_size=-1024;", nullptr, nullptr, nullptr); // ~1 Mo cache
  sqlite3_busy_timeout(db, 250);
  return true;
}

bool db_begin() {
  if (!SPIFFS.begin(true)) return false;
  if (!open_db("/spiffs/station.db")) return false;
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

bool db_reopen_if_needed(const char* path) {
  if (!db || sqlite3_errcode(db) != SQLITE_OK) {
    db_end();
    return open_db(path);
  }
  return true;
}
