#pragma once

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
