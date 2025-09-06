#include <Arduino.h>
#include "tensions.h"

// Définitions des constantes et des pins
const float R1_solar = 100000.0;
const float R2_solar = 47000.0;
const float correctionFactorSolar = (R1_solar + R2_solar) / R2_solar;
const int solarPin = 35;

const float R1 = 220000.0;
const float R2 = 100000.0;
const float correctionFactor = (R1 + R2) / R2;
const int analogPin = 34;

float solaire() {
    int adcValueSolar = analogRead(solarPin);
    float voltageSolar = (adcValueSolar * 3.3) / 4095.0; // Convertit en volts
    voltageSolar *= correctionFactorSolar; // Ajuste pour le diviseur de tension

    Serial.print("Tension du panneau solaire: ");
    Serial.print(voltageSolar);
    Serial.println(" V");

    return voltageSolar;
}

float batterie() {
    int adcValue = analogRead(analogPin);
    float voltage = (adcValue * 3.3) / 4095.0; // Convertit en volts
    voltage *= correctionFactor; // Ajuste pour le diviseur de tension

    Serial.print("Tension de la batterie: ");
    Serial.print(voltage);
    Serial.println(" V");

    return voltage;
}
