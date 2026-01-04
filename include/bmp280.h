#ifndef BMP280_H
#define BMP280_H
#include <Arduino.h>

// Initialise le BMP280 (adresse I2C 0x76 par défaut)
bool initBMP280();

// Lit les mesures arrondies à 2 décimales : température (°C), pression (hPa), altitude (m)
void readBMP280(float &temperature, float &pressure, float &altitude);

#endif
