
#ifndef API_H
#define API_H
#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <stdint.h>


// ========== Snapshot RAM partagé (source unique: main.cpp) ==========
// (mesures live que main.cpp met à jour toutes les 30 s)
struct StationSnapshot {
  // DHT22 / BMP280
  float temp_dht = NAN;     // "temperature" (DHT22)
  float hum = NAN;          // "humidite"   (DHT22)
  float temp_bmp = NAN;     // "tempbmp280" (BMP280)
  float press_hPa = NAN;    // "pression"

  // Vent / pluie
  float wind = 0.0f;        // "anemometre" km/h
  float gust = 0.0f;        // "rafale"     km/h
  int   dir_deg = -1;       // "girouette"  0..359, -1=unknown
  float rain_cum = 0.0f;    // "pluviometre" mm

  // Tensions / rosée
  float batt_v = NAN;       // "tension_batterie"
  float solar_v = NAN;      // "tension_solaire"
  float dew = NAN;          // "pointderosee"

  // Date/heure lisible + horodatage ms de l'échantillon
  char  datetime[20] = {0}; // "YYYY-MM-DD HH:MM:SS"
  unsigned long now_ms = 0; // millis au moment de la mesure
};

// Variable globale définie dans main.cpp (et utilisée dans api.cpp)
extern StationSnapshot g_snap;


// ========== Infos station (déjà utilisées dans ton projet) ==========
struct StationInfo {
  int id = 0;
  String ville;
  int codepostal = 0;
  String description;
  bool diy = false;
  int user_id = 0;
  String user_nom;
  String user_prenom;
  String user_email;
  bool valid = false;

  // Wi‑Fi (mis à jour localement)
  int wifi_rssi = 0;    // dBm
  int wifi_bars = 0;    // 0..4
  int wifi_percent = 0; // 0..100
};

extern StationInfo g_stationInfo;

// Flag OTA (défini dans main.cpp)
extern volatile bool otaInProgress;


// ========== API : fonctions exposées ==========
void setupLocalStationApiHandler(WebServer &server);
void maybeRefreshStationInfo();
bool fetchStationInfoFromRemote();
void startStationInfoBackgroundTask();

// Envois (payloads construits en RAM uniquement)
bool sendLatestRowToApi();
bool sendEtatCapteursToApi();
bool sendLatestEtatStationMeteoToApi();

// Recharge la config API (NVS d’abord, fallback DB si vide — implémenté dans api.cpp)
void apiRefreshConfig();

// Constructions JSON RAM-only
String buildStationJsonPayload();        // /api/localStationInfo
String buildStationJsonPayloadStrict();  // /stationdirect (strict)

// Derniers retours d’envoi
struct PushResult {
  int code = 0;             // code HTTP (ou négatif si erreur locale)
  String body;              // corps de réponse
  unsigned long ts_ms = 0;  // timestamp millis de l’envoi
  String endpoint;          // URL appelée
};
extern PushResult g_lastPushRow;   // dernier /stationdirect
extern PushResult g_lastPushEtat;  // dernier /etatstationmeteo

extern int g_lastPushHttpCode;
extern String g_lastPushHttpBody;
extern unsigned long g_lastPushMillis;

#endif // API_H
