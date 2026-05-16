#pragma once
#include "db_read.h"   // pour AppConfig

bool updateStationDirect(
    int id,
    float tempdht22,
    float humiditer,
    float tempbmp280,
    float pression,
    float lumiere,
    float anemometre,
    float girouette,
    float pluviometre,
    float pointderosee,
    float ghost,
    float tpsvie,
    String ts,
    float rafale
);

bool updateAnemometre(int id, float anemometre);
// Nouvelle fonction pour la table tensions
bool updateTensions(
    int id,
    float tension_batterie,
    float tension_solaire
);

bool updateEtatCapteurs(int id,
                        int capteur_dht22, int capteur_bmp280, int capteur_pluvio,
                        int capteur_girou, int capteur_anemo, float tension_batterie, float tension_solaire);


bool updateModulesInDB(int id,
                       int module_bmp280,
                       int module_dht22,
                       int module_sht40,
                       int module_anemo,
                       int module_girou,
                       int module_pluvio,
                       int module_tension,
                       int module_bitvie);

bool updateActivationApiInDB(int id, int activation_envoi_api);


bool updateAppConfig(const AppConfig& c);  // UPDATE config SET ... WHERE id=1



// bd.h
bool updatePeriodicAtomic(
  int id,
  float tempdht22, float humiditer, float tempbmp280, float pression, float lumiere,
  float anemometre, float girouette, float pluviometre, float pointderosee,
  float ghost, float tpsvie, const String& ts, float rafale,
  float tension_batterie, float tension_solaire,
  int capteur_dht22, int capteur_bmp280, int capteur_pluvio, int capteur_girou, int capteur_anemo
);
