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
  int sht40;   // <-- nouveau
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
                     int &module_bitvie, int &module_sht40);


extern int module_bmp280;
extern int module_dht22;
extern int module_anemo;
extern int module_girou;
extern int module_pluvio;
extern int module_tension;
extern int module_bitvie;
extern int module_sht40;


struct EtatCapteurs {
    int id;
    int module_bmp280;
    int module_dht22;
    int module_anemo;
    int module_girou;
    int module_pluvio;
    int module_tension;
    int module_bitvie;
    int module_sht40;
    int capteur_dht22;
    int capteur_bmp280;
    int capteur_pluvio;
    int capteur_girou;
    int capteur_anemo;
    String log_date_bmp280;
    String log_bmp280;
    String log_date_dht22;
    String log_dht22;
    String log_date_girou;
    String log_girou;
    String log_date_tension;
    String log_tension;
    String log_date_anemo;
    String log_anemo;
    String log_date_pluvio;
    String log_pluvio;
    float tension_solaire;
    float tension_batterie;
};

bool updateModuleVariablesFromDB(int id);
bool readActivationApi(int &activation);
struct EtatStationMeteo;
bool readLatestEtatStationMeteo(EtatStationMeteo& out);
bool readLatestEtatCapteurs(EtatCapteurs& out);

// ----- Config application (WiFi + API) -----
struct AppConfig {
  String ssid_wifi;
  String pass_wifi;
  String ip_wifi;     // si tu veux l'afficher
  String id_station;  // en String (compatible "2.0"), mets int si ta colonne est INTEGER
  String adresse_api;
  String token;
  int    activation_envoi_api; // 0/1
};

bool readAppConfig(AppConfig& out);        // SELECT ... FROM config WHERE id=1

