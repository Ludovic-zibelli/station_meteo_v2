
#pragma once
#include <Arduino.h>

// Initialisation — path: fichier SPIFFS, maxLines: nb max de lignes conservées
void log_init(const char* path = "/log.txt", uint16_t maxLines = 100);

// Ajuster la limite de lignes (ex: 100)
void log_set_max_lines(uint16_t maxLines);

// Ajuster la période de flush en millisecondes (ex: 60000 pour 1 minute)
void log_set_flush_period(uint32_t flushMs);

// Ajouter une ligne (non bloquant, bufferisé en RAM)
void log_line(const String& msg);

// printf-like (éviter collision avec esp32-hal-log.h) : app_logf(...)
void app_logf(const char* fmt, ...);

// Lecture complète (borne maxBytes pour éviter gros chargements)
bool log_read(String& out, size_t maxBytes = 16 * 1024);

// Effacement du journal (et réinitialisation des compteurs)
void log_clear();

// À appeler dans loop() (ou une tâche) pour déclencher le flush minute
void log_tick();

// Forcer un flush immédiat (ex: avant reboot, erreur critique, OTA start/end)
void log_flush();
