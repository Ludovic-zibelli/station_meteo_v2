#ifndef API_H
#define API_H

#include <Arduino.h>

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

bool sendLatestRowToApi();

#endif
