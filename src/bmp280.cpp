#ifndef BMP280_H
#define BMP280_H

#include <Arduino.h>

// Déclare les fonctions pour interagir avec le capteur
bool initBMP280();
void readBMP280(float &temperature, float &pressure, float &altitude);

#endif
