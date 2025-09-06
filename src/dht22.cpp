#include <Arduino.h>
#include "dht22.h"


#define DHTPIN 4       // Pin connectée au DHT22
#define DHTTYPE DHT22    // Type du capteur

// Création de l'objet DHT
DHT dht(DHTPIN, DHTTYPE);

bool dhtStatus = false; 

void initDHT() {
    dht.begin();
    delay(2000); // Attente pour stabilisation du capteur
    float temp = dht.readTemperature();
    if (!isnan(temp)) {
        dhtStatus = true;
        Serial.println("DHT22 initialisé avec succès.");
    } else {
        dhtStatus = false;
        Serial.println("Échec de l'initialisation du DHT22.");
    }

}


bool isDHTReady() {
    return dhtStatus;
}



float getTemperature() {
    if (!dhtStatus) return -999.0;
    float temp = dht.readTemperature();
    return isnan(temp) ? -999.0 : temp;
}

float getHumidity() {
    if (!dhtStatus) return -999.0;
    float hum = dht.readHumidity();
    return isnan(hum) ? -999.0 : hum;
}


