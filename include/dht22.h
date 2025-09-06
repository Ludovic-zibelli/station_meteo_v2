#ifndef DHT_H
//#define DHT_H           //Erreur de chat GPT
#include <DHT.h>         // Inclure la bibliothèque DHT
#include <DHT_U.h>       // Inclure la bibliothèque pour les capteurs unifiés

void initDHT();
float getTemperature();
float getHumidity();
bool isDHTReady();

#endif // DHT_H


