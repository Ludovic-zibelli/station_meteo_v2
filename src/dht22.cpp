#include <Arduino.h>
#include "dht22.h"
#include "log.h"


#define DHTPIN 4       // Pin connectée au DHT22
#define DHTTYPE DHT22    // Type du capteur

// Création de l'objet DHT
DHT dht(DHTPIN, DHTTYPE);


static unsigned long g_dhtWarmupUntil = 0;
static bool g_dhtReady = false;


// Offsets configurables
float humidityOffset = 0.0;   // Corrige l'humidité
float temperatureOffset = 0.0; // Corrige la température

bool dhtStatus = false; 

void initDHT() {
    dht.begin();
    g_dhtWarmupUntil = millis() + 2000; // chauffe non-bloquante
    g_dhtReady = false;

    delay(2000); // Attente pour stabilisation du capteur
    float temp = dht.readTemperature();
    if (!isnan(temp)) {
        dhtStatus = true;
        Serial.println("DHT22 initialisé avec succès.");
        app_logf("DHT22 initialisé avec succès.\n");
    } else {
        dhtStatus = false;
        Serial.println("Échec de l'initialisation du DHT22.");
        app_logf("Échec de l'initialisation du DHT22.\n");
    }

}


bool isDHTReady() {
  if (!g_dhtReady && millis() >= g_dhtWarmupUntil) {
    float t = dht.readTemperature();
    g_dhtReady = !isnan(t);
  }
  return g_dhtReady;
}




float getTemperature() {
    if (!dhtStatus) return -999.0;
    float temp = dht.readTemperature();
    return isnan(temp) ? -999.0 : temp + temperatureOffset;
}



float getHumidity() {
    if (!dhtStatus) return -999.0;
    float hum = dht.readHumidity();
    return isnan(hum) ? -999.0 : hum - humidityOffset;
}



