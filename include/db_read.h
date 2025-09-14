#pragma once
#include <Arduino.h>

// Structure des données station_direct
struct StationDirect {
  int   id;
  float tempdht22;
  float humiditer;
  float tempbmp280;
  float pression;
  float lumiere;
  float anemometre;
  float girouette;
  float pluviometre;
  float pointderosee;
  float ghost;
  float tpsvie;
  String timestamp;   // stocké en texte "YYYY-MM-DD HH:MM:SS" dans SQLite
  float rafale;
};

// Lit la ligne par ID
bool readStationDirectById(int id, StationDirect &out);

// Lit la dernière ligne (par timestamp puis id)
bool readLatestStationDirect(StationDirect &out);


// Structure des modules
struct Modules {
  int bmp280;
  int dht22;
  int anemo;
  int girou;
  int pluvio;
  int tension;
  int bitvie;
};

// Fonction pour lire les modules depuis la table etatcapteurs
bool readModulesById(int id,
                     int &module_bmp280, int &module_dht22, int &module_anemo,
                     int &module_girou, int &module_pluvio, int &module_tension,
                     int &module_bitvie);


extern int module_bmp280;
extern int module_dht22;
extern int module_anemo;
extern int module_girou;
extern int module_pluvio;
extern int module_tension;
extern int module_bitvie;

bool updateModuleVariablesFromDB(int id);


