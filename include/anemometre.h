#pragma once
#include <Arduino.h>

enum AnemoState : uint8_t {
  ANEMO_INIT = 0,
  ANEMO_OK,
  ANEMO_NO_PULSES,
  ANEMO_STUCK_LOW,
  ANEMO_STUCK_HIGH,
  ANEMO_FREQ_TOO_HIGH,
  ANEMO_DISABLED
};

void anemo_init(uint8_t gpioPin,
                float K_mps_per_Hz = 0.6667f,
                float C_mps        = 0.0f,
                uint8_t pulsesPerRev = 1,
                uint32_t samplePeriodMs = 2000,
                float maxFreqHz = 100.0f);

void anemo_update();               // à appeler dans loop()
float anemo_get_speed_mps();
float anemo_get_speed_kmh();
float anemo_get_speed_knots();

float anemo_get_gust_mps();        // rafale max ~10 min
float anemo_get_gust_kmh();
float anemo_get_gust_knots();

AnemoState  anemo_get_state();
const char* anemo_state_str(AnemoState s);

void anemo_enable(bool en);
void anemo_set_sample_period(uint32_t ms);

// (optionnel) réglages anti‑parasites
void anemo_set_deadtime_us(uint32_t us);
void anemo_set_no_pulse_timeout_ms(uint32_t ms);
void anemo_set_stuck_timeout_ms(uint32_t ms);

// --- Helpers "bit d'état" ---
uint8_t anemo_ok_bit_strict();   // 1 si ANEMO_OK, sinon 0
uint8_t anemo_ok_bit_relaxed();  // 1 si ANEMO_OK ou ANEMO_NO_PULSES, sinon 0
