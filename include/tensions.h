#ifndef TENSION_H
#define TENSION_H

// Déclarations des résistances et des pins utilisées
extern const float R1_solar;
extern const float R2_solar;
extern const float correctionFactorSolar;
extern const int solarPin;

extern const float R1;
extern const float R2;
extern const float correctionFactor;
extern const int analogPin;

// Déclarations des fonctions
float solaire();
float batterie();

#endif // TENSION_H
