#ifndef BMP280_H
#define BMP280_H
#include <Arduino.h>


// --- API existante de ta lib ---
bool initBMP280();                                    // déjà présent
void readBMP280(float &tC, float &p_hPa, float &alt_m);

// --- Adresse I2C effectivement utilisée (mise à jour par initBMP280) ---
extern uint8_t g_bmp_addr;                            // 0x76 ou 0x77

// --- Codes d'état BMP (pour tes JSON / logs) ---
enum : int8_t {
  BMP_OK         =  1,   // lecture crédible
  BMP_NAN        = -1,   // valeurs hors plage / NaN
  BMP_ID_BAD     = -2,   // mauvais ID (0xD0 != 0x58/0x56/0x57)
  BMP_BUS_BAD    = -3,   // erreur bus I2C (lecture registre KO)
  BMP_RESETTING  = -4    // soft reset en cours
};

// --- Helpers "santé" et réinit douce ---
uint8_t bmp_read8(uint8_t reg);
bool    bmp_write8(uint8_t reg, uint8_t val);
bool    bmp_soft_reset();                              // 0xE0 = 0xB6
int8_t  bmp_health();                                  // ID + (option) STATUS


#endif
