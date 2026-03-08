
#include <Adafruit_BMP280.h>
#include <Arduino.h>
#include <Wire.h>
#include "bmp280.h"
#include "log.h"

static Adafruit_BMP280 bmp;
uint8_t g_bmp_addr = 0x76;                   // mise à jour dans initBMP280()

// Arrondi à 2 décimales (préserve NaN) — tu l’avais déjà
static inline float round2f(float v) {
  if (isnan(v)) return v;
  return roundf(v * 100.0f) / 100.0f;
}

// ---- Helpers registre (Wire direct, pas de dépendance internes Adafruit) ----
uint8_t bmp_read8(uint8_t reg) {
  Wire.beginTransmission(g_bmp_addr);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return 0xFF;       // erreur bus
  Wire.requestFrom((int)g_bmp_addr, 1);
  if (!Wire.available()) return 0xFF;
  return (uint8_t)Wire.read();
}

bool bmp_write8(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(g_bmp_addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool bmp_soft_reset() {
  bool ok = bmp_write8(0xE0, 0xB6);                   // SOFTRESET
  delay(3);                                           // datasheet: >2ms
  return ok;
}

int8_t bmp_health() {
  uint8_t id = bmp_read8(0xD0);                       // ID = 0x58 (BMP280)
  if (id == 0xFF || id == 0x00) return BMP_BUS_BAD;   // bus KO / adresse fausse
  if (id != 0x58 && id != 0x56 && id != 0x57) return BMP_ID_BAD;
  (void)bmp_read8(0xF3);                              // STATUS (option log)
  return BMP_OK;
}

// ----------------- API lib (inchangée côté main.cpp) -----------------

// Essaie 0x76 puis 0x77 et mémorise l’adresse retenue
bool initBMP280() {
  // Essai 0x76 (ta config actuelle)
  if (bmp.begin(0x76)) {
    g_bmp_addr = 0x76;
  } else if (bmp.begin(0x77)) {
    g_bmp_addr = 0x77;
  } else {
    Serial.println(F("[BMP280] sensor not found!"));
    app_logf("[BMP280] sensor not found!");
    return false;
  }

  // (Optionnel) Réglages de suréchantillonnage/filtre
  // bmp.setSampling(
  //   Adafruit_BMP280::MODE_NORMAL,
  //   Adafruit_BMP280::SAMPLING_X4,  // Temp
  //   Adafruit_BMP280::SAMPLING_X4,  // Press
  //   Adafruit_BMP280::FILTER_X4,    // IIR
  //   Adafruit_BMP280::STANDBY_MS_62_5
  // );

  return true;
}

void readBMP280(float &temperature, float &pressure, float &altitude) {
  // Lecture brute (ton code d’origine)
  float t = bmp.readTemperature();           // °C
  float p = bmp.readPressure() / 100.0f;     // Pa -> hPa
  float a = bmp.readAltitude(1011.9f);       // m (calibre selon QNH local)

  // Arrondi à 2 décimales (ton utilitaire)
  temperature = round2f(t);
  pressure    = round2f(p);
  altitude    = round2f(a);
}