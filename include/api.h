#ifndef API_H
#define API_H

#pragma once
#include <Arduino.h>
#include <WebServer.h>

// structure pour exposer les mesures
struct Measurements {
  float temperature = NAN;
  float humidite = NAN;
  float tempbmp280 = NAN;
  float pression = NAN;
  float pointderosee = NAN;
  float tension_solaire = NAN;
  float tension_batterie = NAN;
  float anemometre = NAN;
  float rafale = NAN;
  float pluviometre = NAN;
  String datetime = String("");
  int tpsvie = 0;
};

extern Measurements g_measurements;

// Déclaration forward si Modules est défini ailleurs
struct Modules;

// Structure de cache partagée (doit correspondre à celle définie dans api.cpp)
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

  // champs wifi ajoutés
  int wifi_rssi = 0;    // dBm
  int wifi_bars = 0;    // 0..4
  int wifi_percent = 0; // 0..100
};

// variable définie dans api.cpp
extern StationInfo g_stationInfo;

// Flag global pour indiquer qu'une OTA est en cours (défini dans main.cpp)
extern volatile bool otaInProgress;

// Fonctions exposées (implémentées dans api.cpp)
void setupLocalStationApiHandler(WebServer &server);
void maybeRefreshStationInfo();
bool fetchStationInfoFromRemote();
void startStationInfoBackgroundTask();

bool sendLatestEtatStationMeteoToApi();
bool sendLatestRowToApi();

bool sendEtatCapteursToApi(
    const Modules& mods,
    int etat_dht22, int etat_bmp280, int etat_pluvio, int etat_girou, int etat_anemo, int ghost,
    String logDateBmp280, String logBmp280,
    String logDateDht22, String logDht22,
    String logDateGirou, String logGirou,
    String logDateTension, String logTension,
    String logDateAnemo, String logAnemo,
    String logDatePluvio, String logPluvio
);

// Déclaration de la fonction qui envoie les données
void envoyerDonneesAPI(
    float tempDHT22,
    float tempBMP280,
    float humidite,
    float pression,
    int lumiere,
    float anemometre,
    int girouette,
    float pluviometre,
    String pointRosee,
    unsigned long tpsvie
);

// construit le JSON de la station et retourne la string (utilise g_stationInfo + g_measurements)
String buildStationJsonPayload();

// dernier retour d'envoi vers l'API distante
extern int g_lastPushHttpCode;
extern String g_lastPushHttpBody;
extern unsigned long g_lastPushMillis;

struct PushResult {
  int code = 0;          // code HTTP (ou négatif pour erreur locale)
  String body = String();
  unsigned long ts_ms = 0; // timestamp du push (millis)
  String endpoint = String(); // endpoint distant (pour info)
};

extern PushResult g_lastPushRow;   // pour sendLatestRowToApi()
extern PushResult g_lastPushEtat;  // pour sendEtatCapteursToApi()

bool sendLatestRowToApi();
bool sendEtatCapteursToApi();
bool sendLatestEtatStationMeteoToApi();
void apiRefreshConfigFromDb();

#endif
