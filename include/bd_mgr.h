// --- db_mgr.h ---
#pragma once
#include <Arduino.h>
#include <sqlite3.h>
#include "SPIFFS.h"

extern sqlite3* db;
extern SemaphoreHandle_t g_dbMutex;

bool db_begin();
void db_end();
bool db_exec(const char* sql);             // exécution simple
bool db_reopen_if_needed(const char* path); // tentative de réouverture si KO
