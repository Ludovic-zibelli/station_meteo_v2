// pluviometre.h
#pragma once
#include <Arduino.h>

#ifndef HALL_SENSOR_PIN
#define HALL_SENSOR_PIN 19 // D19
#endif

void initPluviometre();
void gestionPluviometre();
float obtenirQuantitePluie_mm();
void resetQuantitePluie();

