#include <Adafruit_SHT4x.h>
#include "sht40.h"
#include "log.h"

Adafruit_SHT4x sht4 = Adafruit_SHT4x();
bool sht40Status = false;

bool initSHT40() {
    if (!sht4.begin()) {
        Serial.println("Erreur SHT40 !");
        app_logf("Erreur SHT40 !");
        sht40Status = false;
        return false;
    }
    Serial.println("SHT40 initialisé avec succès.");
    app_logf("SHT40 initialisé avec succès.");
    sht40Status = true;
    return true;
}

float getSHT40Temperature() {
    if (!sht40Status) return -999.0;
    sensors_event_t humidity, temp;
    sht4.getEvent(&humidity, &temp);
    return temp.temperature;
}

float getSHT40Humidity() {
    if (!sht40Status) return -999.0;
    sensors_event_t humidity, temp;
    sht4.getEvent(&humidity, &temp);
    return humidity.relative_humidity;
}