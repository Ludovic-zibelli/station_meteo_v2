
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

// Renvoie true si une ligne a été trouvée.
//  - tag : sous-chaîne à chercher dans la ligne (ex: "BMP280", "DHT22", ...)
//  - outDateIsoTz : "YYYY-MM-DDTHH:MM:SS+HH:MM"
//  - outMsg : le message (sans l'horodatage)
bool log_find_last_by_tag(const char* tag, String& outDateIsoTz, String& outMsg);

// --- Rotation & purge journalière ---
// Renvoie le chemin du fichier de log du jour : "/log-YYYYMMDD.txt"
String log_daily_path();

// Purge les fichiers de log plus anciens que 'keepDays' jours (ex: 14)
bool log_purge_old(uint16_t keepDays);

// Lire un fichier de log spécifique (ex: "/log-20260203.txt")
// maxBytes=0 pour tout le fichier.
bool log_read_file(const char* path, String& out, size_t maxBytes = 0);