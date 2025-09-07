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
