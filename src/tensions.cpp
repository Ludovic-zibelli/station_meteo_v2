#include <Arduino.h>
#include "tensions.h"

// === Défs "extern" attendues par tensions.h (mêmes noms & types) ===
// Solaire (haut/bas) : adapte si tes valeurs réelles diffèrent
const float R1_solar = 100000.0f;     // 100 kΩ
const float R2_solar =  47000.0f;     // 47 kΩ
const float correctionFactorSolar = (R1_solar + R2_solar) / R2_solar;   // ≈ 3.1277
const int   solarPin = 35;

// Batterie (haut/bas) : 200k/100k -> facteur = 3.0 (si tu as vraiment 220k, remets 220000.0f)
const float R1 = 200000.0f;           // 200 kΩ   <-- mets 220000.0f si ton montage est 220k/100k
const float R2 = 100000.0f;           // 100 kΩ
const float correctionFactor = (R1 + R2) / R2;                           // 3.0 par défaut
const int   analogPin = 34;

// === Calibration fine (gain + offset), optionnelle ===
// Laisse 1.0 / 0.0 d'abord, puis ajuste kSolar/kBatt d'après ton multimètre.
static float kSolar = 1.0f, oSolar = 0.0f;
static float kBatt  = 1.0f, oBatt  = 0.0f;

// Lecture moyenne calibrée (ADC en mV) -> V au pin -> V en amont du diviseur -> gain/offset
static float readVoltDiv_mV(int pin, float factor, float k, float o) {
  analogSetPinAttenuation(pin, ADC_11db);   // FS ~ 3.6 V, calibré pour analogReadMilliVolts
  const int N = 16;
  uint32_t acc = 0;
  for (int i = 0; i < N; ++i) { acc += analogReadMilliVolts(pin); delay(2); }
  float v_adc = (acc / (float)N) / 1000.0f; // V au pin ADC après diviseur
  float v_in  = v_adc * factor;             // V avant diviseur
  return v_in * k + o;                      // calibration fine
}

float solaire() {
  return readVoltDiv_mV(solarPin, correctionFactorSolar, kSolar, oSolar);
}

float batterie() {
  return readVoltDiv_mV(analogPin, correctionFactor,       kBatt,  oBatt);
}

// ---------- Échantillonnage asynchrone & cache ----------
static TaskHandle_t s_tensionTask = nullptr;
static volatile float s_solar_cached = NAN;
static volatile float s_batt_cached  = NAN;
static volatile uint32_t s_period_ms = 1000;

static void tensions_task(void*){
  for(;;){
    // 1) Mesures “riches” (tes fonctions existantes avec moyenne/atténuation)
    float vsol  = solaire();
    float vbatt = batterie();

    // 2) Mettre à jour les caches (section critique très courte)
    s_solar_cached = vsol;
    s_batt_cached  = vbatt;

    // 3) Attente coopérative
    vTaskDelay(pdMS_TO_TICKS(s_period_ms));
  }
}

void tensions_begin_async(uint32_t period_ms){
  s_period_ms = period_ms ? period_ms : 1000;
  if (s_tensionTask) return; // déjà lancée
  xTaskCreatePinnedToCore(tensions_task, "tensions",
                          4096, nullptr, 1, &s_tensionTask, 1);
}

float solaire_cached(){
  float v = s_solar_cached;
  // fallback initial si pas encore d’échantillon
  if (isnan(v)) v = solaire(); // 1ère fois seulement
  return v;
}

float batterie_cached(){
  float v = s_batt_cached;
  if (isnan(v)) v = batterie();
  return v;
}