// pluviometre.cpp
#include "pluviometre.h"
#include "log.h"

// ---- Configuration ----
// À CALIBRER en fonction de ton auget : mm de pluie par bascule
static constexpr float MM_PAR_BASCULE = 0.2794f; // exemple: 0.2794 mm/tip (0.01")
// Anti-rebond en µs (fenêtre minimum entre deux bascules)
static constexpr uint32_t DEBOUNCE_US = 300000UL; // 300 ms

// Variables partagées ISR/app
volatile uint32_t g_tipCount = 0;
volatile uint32_t g_lastTipUs = 0;

// Accumulateur total (mm)
static float g_pluieTotale_mm = 0.0f;

void IRAM_ATTR isrPluviometre() {
    const uint32_t now = micros();
    if (now - g_lastTipUs > DEBOUNCE_US) {   // <-- ici
        g_lastTipUs = now;
        g_tipCount++;
    }
}


void initPluviometre() {
    pinMode(HALL_SENSOR_PIN, INPUT_PULLUP); // pull-up interne (prévoir 10k externe si câble long)
    // Si la sortie du A3144E passe à LOW quand l'aimant est présent -> FALLING
    attachInterrupt(digitalPinToInterrupt(HALL_SENSOR_PIN), isrPluviometre, FALLING);
    Serial.println("[Girou] Pluviomètre initialisé sur D19 (interrupt + anti-rebond).");
   app_logf("[Girou] Pluviomètre initialisé sur D19 (interrupt + anti-rebond).");
}

void gestionPluviometre() {
    // Capture atomique des tips
    noInterrupts();
    uint32_t tips = g_tipCount;
    g_tipCount = 0;
    interrupts();

    if (tips > 0) {
        float delta_mm = tips * MM_PAR_BASCULE;
        g_pluieTotale_mm += delta_mm;
        Serial.printf("[Pluie] +%.3f mm (tips=%lu) -> total=%.3f mm\n",
                      delta_mm, (unsigned long)tips, g_pluieTotale_mm);
        app_logf("[Pluie] +%.3f mm (tips=%lu) -> total=%.3f mm",
                   delta_mm, (unsigned long)tips, g_pluieTotale_mm);
    }
}

float obtenirQuantitePluie_mm() {
    return g_pluieTotale_mm;
}

void resetQuantitePluie() {
    g_pluieTotale_mm = 0.0f;
}

// --- Bit d'état instantané : 1 si le capteur est "actif" (aimant présent) ---
uint8_t pluvio_active_bit() {
    // Assure-toi que initPluviometre() a déjà été appelé (pin en INPUT_PULLUP)
    // LOW = aimant présent devant le capteur (sortie collecteur ouvert à la masse)
    return (digitalRead(HALL_SENSOR_PIN) == LOW) ? 1 : 0;
}
