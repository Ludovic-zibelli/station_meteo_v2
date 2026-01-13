
// log.cpp

#include "log.h"
#include <SPIFFS.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char* s_path = "/log.txt";
static uint16_t    s_maxLines = 100;
static uint32_t    s_flushMs  = 60000;        // période par défaut : 60 s
static SemaphoreHandle_t s_mutex = nullptr;

// Buffer RAM — accumulateur
static String s_buf;
static unsigned long s_lastFlush = 0;

// Compteur de lignes approximatif dans le fichier (pour décider du trim)
static uint32_t s_lineCount = 0;

// Timestamp local (non bloquant)
static String isoNow() {
  struct tm ti;
  if (getLocalTime(&ti, 0)) { // pas d'attente
    char buf[20];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
             ti.tm_hour, ti.tm_min, ti.tm_sec);
    return String(buf);
  }
  // fallback si NTP/TZ pas dispo
  char fb[24];
  snprintf(fb, sizeof(fb), "ms:%lu", (unsigned long)millis());
  return String(fb);
}

static bool take(uint32_t ms = 2) {
  if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
  return s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(ms)) == pdTRUE;
}
static void give() { if (s_mutex) xSemaphoreGive(s_mutex); }

// Trim — garde les N dernières lignes (opération au flush uniquement)
static void trim_file_keep_last_n() {
  File f = SPIFFS.open(s_path, "r");
  if (!f) return;
  String all = f.readString();
  f.close();

  int count = 0;
  for (size_t i = 0; i < all.length(); ++i) if (all[i] == '\n') ++count;
  s_lineCount = count; // mise à jour du compteur

  if (count <= s_maxLines) return;

  int toSkip = count - s_maxLines;
  size_t pos = 0;
  while (toSkip > 0 && pos < all.length()) {
    if (all[pos++] == '\n') --toSkip;
  }
  File w = SPIFFS.open(s_path, "w");
  if (!w) return;
  w.print(all.substring(pos));
  w.close();

  s_lineCount = s_maxLines;
}

// Flush interne (mutex déjà pris)
static void flush_locked() {
  if (s_buf.isEmpty()) return;

  File f = SPIFFS.open(s_path, FILE_APPEND);
  if (!f) f = SPIFFS.open(s_path, "w");
  if (f) {
    f.print(s_buf);
    f.close();
    for (size_t i = 0; i < s_buf.length(); ++i) if (s_buf[i] == '\n') ++s_lineCount;
  }

  s_buf = "";
  s_lastFlush = millis();

  // Trim seulement si > maxLines + hystérèse (limite + 20 pour éviter trim trop fréquent)
  if (s_lineCount > (uint32_t)(s_maxLines + 20)) {
    trim_file_keep_last_n();
  }
}

void log_init(const char* path, uint16_t maxLines) {
  s_path = path ? path : "/log.txt";
  s_maxLines = maxLines ? maxLines : 100;
  s_buf.reserve(1024);

  // Monte SPIFFS (une fois)
  SPIFFS.begin(true);
  if (!s_mutex) s_mutex = xSemaphoreCreateMutex();

  // Crée le fichier si absent et initialise s_lineCount
  if (!SPIFFS.exists(s_path)) {
    File f = SPIFFS.open(s_path, "w");
    if (f) f.close();
    s_lineCount = 0;
  } else {
    File f = SPIFFS.open(s_path, "r");
    s_lineCount = 0;
    while (f && f.available()) if (f.read() == '\n') ++s_lineCount;
    if (f) f.close();
  }
  s_lastFlush = millis();
}

void log_set_max_lines(uint16_t maxLines) {
  if (!maxLines) return;
  if (take()) { s_maxLines = maxLines; give(); }
}

void log_set_flush_period(uint32_t flushMs) {
  if (flushMs < 5000) flushMs = 5000; // éviter flush trop fréquents
  if (take()) { s_flushMs = flushMs; give(); }
}

void log_line(const String& msg) {
  // Pas d'echo Serial pour éviter blocages — à réactiver si besoin
  // Serial.println(msg);

  String line = isoNow() + " | " + msg + "\n";

  if (!take()) return;         // si occupé, on abandonne (non bloquant)
  s_buf += line;
  give();
}

void app_logf(const char* fmt, ...) {
  char buf[256];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  log_line(String(buf));
}

bool log_read(String& out, size_t maxBytes) {
  out = "";
  if (!take()) return false;
  File f = SPIFFS.open(s_path, "r");
  if (!f) { give(); return false; }

  size_t sz = f.size();
  if (maxBytes && sz > maxBytes) f.seek(sz - maxBytes);
  out.reserve((maxBytes && sz > maxBytes) ? maxBytes : sz);
  out = f.readString();
  f.close();
  give();
  return true;
}

void log_clear() {
  if (!take()) return;
  if (SPIFFS.exists(s_path)) SPIFFS.remove(s_path);
  File f = SPIFFS.open(s_path, "w");
  if (f) f.close();
  s_buf = "";
  s_lineCount = 0;
  s_lastFlush = millis();
  give();
}

void log_tick() {
  if (!take()) return;
  if (!s_buf.isEmpty() && (millis() - s_lastFlush >= s_flushMs)) {
    flush_locked();
  }
  give();
}

void log_flush() {
  if (!take()) return;
  flush_locked();
  give();
}
