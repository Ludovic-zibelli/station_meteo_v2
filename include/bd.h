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