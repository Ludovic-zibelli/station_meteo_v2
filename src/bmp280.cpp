
#include <Adafruit_BMP280.h>
#include <Arduino.h>
#include "bmp280.h"
#include "log.h"

static Adafruit_BMP280 bmp;

// Helper : arrondi à 2 décimales (préserve NaN)
static inline float round2f(float v) {
  if (isnan(v)) return v;
  return roundf(v * 100.0f) / 100.0f;
}

bool initBMP280() {
  // Adresse 0x76 (ta configuration)
  if (!bmp.begin(0x76)) {
    Serial.println(F("BMP280 sensor not found!"));
    app_logf("BMP280 sensor not found!");
    return false;
  }

  // (Optionnel) Échantillonnage plus stable
  // Oversampling / filtrage — à adapter selon ta fréquence de lecture :
  // bmp.setSampling(
  //   Adafruit_BMP280::MODE_NORMAL,
  //   Adafruit_BMP280::SAMPLING_X4,   // Temp OSR
  //   Adafruit_BMP280::SAMPLING_X4,   // Press OSR
  //   Adafruit_BMP280::FILTER_X4,     // IIR
  //   Adafruit_BMP280::STANDBY_MS_62_5
  // );

  return true;
}

void readBMP280(float &temperature, float &pressure, float &altitude) {
  // Lecture brute
  float t = bmp.readTemperature();        // °C
  float p = bmp.readPressure() / 100.0f;  // Pa -> hPa
  float a = bmp.readAltitude(1011.9f);    // m (à adapter à ta pression de référence locale)

  // Arrondi à 2 décimales
  temperature = round2f(t);
  pressure    = round2f(p);
  altitude    = round2f(a);
}
