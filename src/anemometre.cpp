#include "anemometre.h"

static uint8_t  s_pin = 18;
static float    s_K   = 0.6667f;
static float    s_C   = 0.0f;
static uint8_t  s_pulsesPerRev = 1;
static uint32_t s_sampleMs = 2000;
static float    s_maxFreqHz  = 100.0f;

static volatile uint32_t s_pulseCount = 0;
static volatile uint32_t s_lastEdgeUs = 0;

static uint32_t s_deadtimeUs        = 120;    // anti‑glitch
static uint32_t s_noPulseTimeoutMs  = 10000;  // 10 s sans pulse -> NO_PULSES
static uint32_t s_stuckTimeoutMs    = 20000;  // 20 s niveau constant -> STUCK

static uint32_t s_lastSampleMs = 0;
static float    s_lastSpeedMps = 0.0f;

// Suivi état
static AnemoState s_state = ANEMO_INIT;
static bool       s_enabled = true;

// Suivi niveau pour “stuck”
static bool     s_lastLevel = true;   // pull-up -> HIGH au repos
static uint32_t s_levelSinceMs = 0;

// Buffer rafales (~10 min)
static constexpr uint32_t GUST_WINDOW_MS = 10UL * 60UL * 1000UL;
static const size_t MAX_SAMPLES = 400;
static float   s_ring[MAX_SAMPLES];
static size_t  s_ringSize = 0;
static size_t  s_ringHead = 0;

static void IRAM_ATTR anemo_isr() {
  const uint32_t now = micros();
  if (now - s_lastEdgeUs > s_deadtimeUs) {
    s_pulseCount++;
    s_lastEdgeUs = now;
  }
}

static inline void push_speed_sample(float v) {
  s_ring[s_ringHead] = v;
  s_ringHead = (s_ringHead + 1) % s_ringSize;
}

void anemo_init(uint8_t gpioPin, float K_mps_per_Hz, float C_mps, uint8_t pulsesPerRev,
                uint32_t samplePeriodMs, float maxFreqHz) {
  s_pin          = gpioPin;
  s_K            = K_mps_per_Hz;
  s_C            = C_mps;
  s_pulsesPerRev = pulsesPerRev;
  s_sampleMs     = samplePeriodMs;
  s_maxFreqHz    = maxFreqHz;

  s_enabled      = true;
  s_state        = ANEMO_INIT;
  s_pulseCount   = 0;
  s_lastEdgeUs   = micros();

  pinMode(s_pin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(s_pin), anemo_isr, FALLING); // Hall open-collector

  s_lastSampleMs = millis();

  // init buffer rafales
  s_ringSize = min(MAX_SAMPLES, (size_t)((GUST_WINDOW_MS + s_sampleMs - 1) / s_sampleMs));
  for (size_t i=0;i<s_ringSize;i++) s_ring[i] = 0.0f;
  s_ringHead = 0;
  s_lastSpeedMps = 0.0f;

  // init suivi niveau
  s_lastLevel = digitalRead(s_pin);
  s_levelSinceMs = millis();
}

void anemo_update() {
  if (!s_enabled) {
    s_state = ANEMO_DISABLED;
    return;
  }

  const uint32_t nowMs = millis();

  // ---- Surveillance niveau (STUCK) ----
  bool level = digitalRead(s_pin);
  if (level != s_lastLevel) {
    s_lastLevel = level;
    s_levelSinceMs = nowMs;
  } else {
    const uint32_t levelDur = nowMs - s_levelSinceMs;
    if (levelDur >= s_stuckTimeoutMs) {
      s_state = level ? ANEMO_STUCK_HIGH : ANEMO_STUCK_LOW;
    }
  }

  // ---- Fenêtre d’échantillonnage ----
  if ((nowMs - s_lastSampleMs) < s_sampleMs) return;

  const float dt_s = (nowMs - s_lastSampleMs) / 1000.0f;
  s_lastSampleMs = nowMs;

  noInterrupts();
  uint32_t pulses = s_pulseCount;
  s_pulseCount = 0;
  interrupts();

  // Fréquence brute
  float freq_hz = (dt_s > 0.f) ? (pulses / dt_s) : 0.0f;
  if (s_pulsesPerRev > 1) {
    freq_hz /= s_pulsesPerRev;
  }

  // Vitesse
  s_lastSpeedMps = s_K * freq_hz + s_C;
  if (s_lastSpeedMps < 0) s_lastSpeedMps = 0;

  // Rafales
  push_speed_sample(s_lastSpeedMps);

  // ---- Etat priorisé ----
  if (freq_hz > s_maxFreqHz * 1.05f) {
    s_state = ANEMO_FREQ_TOO_HIGH;
    return;
  }

  const uint32_t msSinceLastPulse = (millis() - (s_lastEdgeUs / 1000)); // approx
  if (msSinceLastPulse >= s_noPulseTimeoutMs) {
    if (s_state != ANEMO_STUCK_HIGH && s_state != ANEMO_STUCK_LOW) {
      s_state = ANEMO_NO_PULSES; // calme plat ou débranché
    }
    return;
  }

  s_state = ANEMO_OK;
}

float anemo_get_speed_mps()   { return s_lastSpeedMps; }
float anemo_get_speed_kmh()   { return s_lastSpeedMps * 3.6f; }
float anemo_get_speed_knots() { return s_lastSpeedMps * 1.943844f; }

static float compute_gust_from_ring() {
  float g = 0.0f;
  for (size_t i=0; i<s_ringSize; ++i) if (s_ring[i] > g) g = s_ring[i];
  return g;
}

float anemo_get_gust_mps()    { return compute_gust_from_ring(); }
float anemo_get_gust_kmh()    { return anemo_get_gust_mps() * 3.6f; }
float anemo_get_gust_knots()  { return anemo_get_gust_mps() * 1.943844f; }

AnemoState anemo_get_state()  { return s_state; }

const char* anemo_state_str(AnemoState s) {
  switch (s) {
    case ANEMO_INIT:           return "INIT";
    case ANEMO_OK:             return "OK";
    case ANEMO_NO_PULSES:      return "NO_PULSES";
    case ANEMO_STUCK_LOW:      return "STUCK_LOW";
    case ANEMO_STUCK_HIGH:     return "STUCK_HIGH";
    case ANEMO_FREQ_TOO_HIGH:  return "FREQ_TOO_HIGH";
    case ANEMO_DISABLED:       return "DISABLED";
    default:                   return "UNKNOWN";
  }
}

void anemo_enable(bool en) { s_enabled = en; if (!en) s_state = ANEMO_DISABLED; }

void anemo_set_sample_period(uint32_t ms) {
  s_sampleMs = ms;
  s_ringSize = min(MAX_SAMPLES, (size_t)((GUST_WINDOW_MS + s_sampleMs - 1) / s_sampleMs));
  if (s_ringHead >= s_ringSize) s_ringHead = 0;
}

void anemo_set_deadtime_us(uint32_t us)         { s_deadtimeUs = us; }
void anemo_set_no_pulse_timeout_ms(uint32_t ms) { s_noPulseTimeoutMs = ms; }
void anemo_set_stuck_timeout_ms(uint32_t ms)    { s_stuckTimeoutMs = ms; }

// ---- Helpers "bit d'état" ----
uint8_t anemo_ok_bit_strict() {
  return (s_state == ANEMO_OK) ? 1 : 0;
}

uint8_t anemo_ok_bit_relaxed() {
  return (s_state == ANEMO_OK || s_state == ANEMO_NO_PULSES) ? 1 : 0;
}
