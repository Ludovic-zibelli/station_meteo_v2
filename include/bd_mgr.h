// --- db_mgr.h ---
#pragma once
#include <Arduino.h>
#include <sqlite3.h>
#include "SPIFFS.h"


// --- types externs ---
extern sqlite3* db;
extern SemaphoreHandle_t g_dbMutex;

// --- API DB existante ---
bool db_begin();
void db_end();
bool db_exec(const char* sql);
bool db_reopen_if_needed(const char* path);

// --- Helpers intégrité/réparation (déjà ajoutés) ---
bool fileLooksLikeSQLite(const char* path = "/spiffs/station.db");
bool dbIntegrityCheck();
bool recreateDatabaseFile();
bool db_try_repair_if_notadb();

// --- Sauvegarde NVS de la configuration (si appelées depuis bd.cpp) ---
struct AppConfig;  // forward declaration (définition dans db_read.h)
void backupConfigToNVS(const AppConfig& c);
bool loadConfigFromNVS(AppConfig& out);
