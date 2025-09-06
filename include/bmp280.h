
//#include <Adafruit_BMP085.h>
#include <Adafruit_BMP280.h>
#include <Arduino.h>

// Déclaration de l'objet global pour le capteur
Adafruit_BMP280 bmp;

// Fonction pour initialiser le BMP280
bool initBMP280() {
    if (!bmp.begin(0x76)) {
        Serial.println(F("BMP280 sensor not found!"));
        return false;
    }
    return true;
}

// Fonction pour lire les données
void readBMP280(float &temperature, float &pressure, float &altitude) {
    temperature = bmp.readTemperature();
    pressure = bmp.readPressure() / 100.0; // Conversion Pa -> hPa
    altitude = bmp.readAltitude(1011.9);
}
