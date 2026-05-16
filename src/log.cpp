
// log.cpp

#include "log.h"
#include <FS.h>
#include <LittleFS.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"


#include <Arduino.h>

#include <time.h>



// Chemin du fichier de log du jour : "/log-YYYYMMDD.txt"
String log_daily_path();

// Purger les fichiers de log plus anciens que 'keepDays' (ex: 14)
bool log_purge_old(uint16_t keepDays);

// Lire N derniers octets d'un fichier spécifique (ex: "/log-20260203.txt")
// Retourne false si fichier absent ou lecture impossible.
bool log_read_file(const char* path, String& out, size_t maxBytes);


static String s_path = "/log.txt";
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
  String p = log_daily_path();
  File f = LittleFS.open(p, "r");
  if (!f) return;

  String all = f.readString();
  f.close();

  int count = 0;
  for (size_t i = 0; i < all.length(); ++i) {
    if (all[i] == '\n') ++count;
  }
  s_lineCount = count; // mise à jour du compteur

  if (count <= s_maxLines) return;

  int toSkip = count - s_maxLines;
  size_t pos = 0;
  while (toSkip > 0 && pos < all.length()) {
    if (all[pos++] == '\n') --toSkip;
  }

  File w = LittleFS.open(p, "w");
  if (!w) return;
  w.print(all.substring(pos));
  w.close();

  s_lineCount = s_maxLines;
}

// Flush interne (mutex déjà pris)
static bool flush_locked() {
  if (s_buf.isEmpty()) return true;

  String p = log_daily_path();
  uint32_t heapBefore = ESP.getFreeHeap();
  Serial.printf("LittleFS info: total=%u used=%u free=%u\n", LittleFS.totalBytes(), LittleFS.usedBytes(), LittleFS.totalBytes() - LittleFS.usedBytes());
  if (!LittleFS.exists(p)) Serial.printf("File %s does not exist, will create\n", p.c_str());
  File f = LittleFS.open(p, FILE_APPEND);
  if (!f) f = LittleFS.open(p, "w");
  if (!f) {
    Serial.printf("[LOG] flush open failed (heap=%u total=%u used=%u free=%u), retrying mount\n",
                  heapBefore,
                  (unsigned)LittleFS.totalBytes(),
                  (unsigned)LittleFS.usedBytes(),
                  (unsigned)(LittleFS.totalBytes() - LittleFS.usedBytes()));
    LittleFS.begin(true);
    delay(10);
    f = LittleFS.open(p, FILE_APPEND);
    if (!f) f = LittleFS.open(p, "w");
  }
  if (!f) {
    Serial.printf("[LOG] flush failed: cannot open %s (heap=%u total=%u used=%u free=%u)\n",
                  p.c_str(),
                  ESP.getFreeHeap(),
                  (unsigned)LittleFS.totalBytes(),
                  (unsigned)LittleFS.usedBytes(),
                  (unsigned)(LittleFS.totalBytes() - LittleFS.usedBytes()));
    // Fallback: log to Serial
    Serial.print("[LOG FALLBACK] ");
    Serial.print(s_buf);
    s_buf = "";
    return true; // Consider it flushed to avoid blocking
  }

  if (f.print(s_buf) < 0) {
    Serial.printf("[LOG] flush failed: write error %s\n", p.c_str());
    f.close();
    return false;
  }

  f.close();
  for (size_t i = 0; i < s_buf.length(); ++i) if (s_buf[i] == '\n') ++s_lineCount;
  s_buf = "";
  s_lastFlush = millis();

  // Trim seulement si > maxLines + hystérèse (limite + 20 pour éviter trim trop fréquent)
  if (s_lineCount > (uint32_t)(s_maxLines + 20)) {
    trim_file_keep_last_n();
  }
  return true;
}

void log_init(const char* path, uint16_t maxLines) {
  s_path = path && *path ? String(path) : String("/log.txt");
  s_maxLines = maxLines ? maxLines : 100;
  s_buf.reserve(1024);

  // Monte LittleFS (une fois)
  bool spiffsOk = LittleFS.begin();
  if (!spiffsOk) {
    Serial.println("[LOG] LittleFS mount failed, trying format");
    spiffsOk = LittleFS.begin(true);
    if (!spiffsOk) {
      Serial.println("[LOG] LittleFS format failed");
    } else {
      Serial.println("[LOG] LittleFS formatted successfully");
    }
  } else {
    Serial.println("[LOG] LittleFS mounted successfully");
  }
  if (spiffsOk) {
    Serial.printf("[LOG] LittleFS mounted total=%u used=%u free=%u\n",
                  (unsigned)LittleFS.totalBytes(),
                  (unsigned)LittleFS.usedBytes(),
                  (unsigned)(LittleFS.totalBytes() - LittleFS.usedBytes()));
  }
  
  {
    String p = log_daily_path();
    if (!LittleFS.exists(p)) {
      File f = LittleFS.open(p, "w"); if (f) f.close();
    }
    // Compte lignes du fichier du jour (si tu veux continuer à limiter le nb de lignes locales)
    File f = LittleFS.open(p, "r");
    s_lineCount = 0;
    while (f && f.available()) if (f.read() == '\n') ++s_lineCount;
    if (f) f.close();
  }

  // Purge des vieux fichiers (> keepDays)
  log_purge_old(14);

  s_lastFlush = millis();

  if (!s_mutex) s_mutex = xSemaphoreCreateMutex();


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
  Serial.println(msg);

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

  // Flush pending log lines vers le fichier avant lecture.
  if (!s_buf.isEmpty()) flush_locked();

  String p = log_daily_path();
  File f = LittleFS.open(p, "r");
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
  String p = log_daily_path();
  if (LittleFS.exists(p)) LittleFS.remove(p);
  File f = LittleFS.open(p, "w"); 
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

// === Helpers internes pour /etatstationmeteo ===
static String currentTzOffsetIso() {
  // Renvoie "+HH:MM" ou "-HH:MM" (ex: +01:00)
  struct tm ti;
  if (getLocalTime(&ti, 0)) {
    char zbuf[8] = {0};  // ex: "+0100"
    strftime(zbuf, sizeof(zbuf), "%z", &ti);
    // insère ':' -> "+01:00"
    String s(zbuf);
    if (s.length() == 5) s = s.substring(0,3) + ":" + s.substring(3);
    return s;
  }
  // défaut : UTC
  return String("+00:00");
}

static bool parse_log_line(const String& line, String& outDateIsoTz, String& outMsg) {
  // Format attendu : "YYYY-MM-DD HH:MM:SS message..."
  if (line.length() < 21) return false;
  // extrait datetime (19 chars) + espace
  String d = line.substring(0, 19);        // "YYYY-MM-DD HH:MM:SS"
  if (d[4] != '-' || d[7] != '-' || d[10] != ' ' || d[13] != ':' || d[16] != ':')
    return false;
  String msg = line.substring(20);
  msg.trim();
  // Convertit en "YYYY-MM-DDTHH:MM:SS+HH:MM" (timezone local)
  String tz = currentTzOffsetIso();
  outDateIsoTz = d.substring(0,10) + "T" + d.substring(11) + tz;
  outMsg = msg;
  return true;
}

bool log_find_last_by_tag(const char* tag, String& outDateIsoTz, String& outMsg) {
  outDateIsoTz = "";
  outMsg = "";
  if (!tag || !*tag) return false;

  // Lisons seulement la fin du fichier (ex: 16 Ko) pour ne pas bloquer
  const size_t TAIL = 16 * 1024;

  if (!take()) return false;
  String p = log_daily_path();
  File f = LittleFS.open(p, "r");
  if (!f) { give(); return false; }

  size_t sz = f.size();
  if (sz == 0) { f.close(); give(); return false; }
  if (sz > TAIL) f.seek(sz - TAIL);

  // On lit ligne par ligne et on mémorise la dernière qui matche
  String last;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (line.indexOf(tag) >= 0) last = line;
  }
  f.close();
  give();

  if (last.length() == 0) return false;
  return parse_log_line(last, outDateIsoTz, outMsg);
}


// --- Format nom fichier du jour ---
static String daily_path_for(time_t t) {
  struct tm ti;
  // Ne créer un fichier journal daté que si l'heure est raisonnable
  // (évite /log-19700101.txt quand le RTC/NTP n'est pas encore initialisé).
  if (t >= 1609459200 && localtime_r(&t, &ti) && ti.tm_year >= 120) {
    String base = s_path;
    int slash = base.lastIndexOf('/');
    int dot = base.lastIndexOf('.');
    String stem = base;
    String ext = "";
    if (dot > slash) {
      ext = base.substring(dot);
      stem = base.substring(0, dot);
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%s-%04d%02d%02d%s",
             stem.c_str(), ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ext.c_str());
    return String(buf);
  }
  // Fallback si horloge pas à l'heure ou date invalide
  return s_path;
}

String log_daily_path() {
  time_t now = time(nullptr);
  return daily_path_for(now);
}

bool log_read_file(const char* path, String& out, size_t maxBytes) {
  out = "";
  if (!path || !*path) return false;
  if (!take()) return false;

  // S'assurer que le buffer en RAM est flushé avant lecture
  if (!s_buf.isEmpty()) flush_locked();

  File f = LittleFS.open(path, "r");
  if (!f) {
    Serial.printf("[LOG] read failed: cannot open %s (heap=%u total=%u used=%u free=%u)\n",
                  path,
                  ESP.getFreeHeap(),
                  (unsigned)LittleFS.totalBytes(),
                  (unsigned)LittleFS.usedBytes(),
                  (unsigned)(LittleFS.totalBytes() - LittleFS.usedBytes()));
    give();
    return false;
  }
  size_t sz = f.size();
  if (maxBytes && sz > maxBytes) f.seek(sz - maxBytes);
  out.reserve((maxBytes && sz > maxBytes) ? maxBytes : sz);
  out = f.readString();
  f.close();
  give();
  return true;
}

static bool parse_date_from_logname(const char* name, struct tm& out) {
  // attend "/log-YYYYMMDD.txt"
  // positions: 0:/ 1:l 2:o 3:g 4:- 5:Y 6:Y 7:Y 8:Y 9:M 10:M 11:D 12:D ...
  if (!name) return false;
  String s(name);
  if (!s.startsWith("/log-") || !s.endsWith(".txt") || s.length() < 17) return false;
  int Y = s.substring(5, 9).toInt();
  int M = s.substring(9, 11).toInt();
  int D = s.substring(11, 13).toInt();
  if (Y < 2000 || M < 1 || M > 12 || D < 1 || D > 31) return false;
  memset(&out, 0, sizeof(out));
  out.tm_year = Y - 1900;
  out.tm_mon  = M - 1;
  out.tm_mday = D;
  out.tm_hour = 0; out.tm_min = 0; out.tm_sec = 0;
  return true;
}

bool log_purge_old(uint16_t keepDays) {
  time_t now = time(nullptr);
  if (now <= 0) return false; // horloge pas prête -> on reporte
  time_t cutoff = now - (time_t)keepDays * 24 * 3600;

  File root = LittleFS.open("/");
  if (!root) return false;

  size_t removed = 0;
  while (true) {
    File f = root.openNextFile();
    if (!f) break;
    String name = f.name();
    f.close();

    struct tm d;
    if (parse_date_from_logname(name.c_str(), d)) {
      time_t t = mktime(&d); // local time OK
      if (t > 0 && t < cutoff) {
        LittleFS.remove(name);
        ++removed;
      }
    }
  }
  return true;
}