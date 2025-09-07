#ifndef GIROUETTE_H
#define GIROUETTE_H

#include <Arduino.h>

enum class Dir8 : int8_t {
  N  = 0,
  NE = 1,
  E  = 2,
  SE = 3,
  S  = 4,
  SW = 5,
  W  = 6,
  NW = 7,
  UNKNOWN = -1
};

class Girouette {
public:
  static constexpr uint8_t NUM_SWITCHES = 8;

  // pins : ordre recommandé => {N, NE, E, SE, S, SW, W, NW}
  // invertLogic=true : actif quand la pin lit LOW (ex: reed vers GND avec INPUT_PULLUP)
  // usePullups=true : active INPUT_PULLUP au begin()
  Girouette(const uint8_t pins[NUM_SWITCHES], bool invertLogic = true, bool usePullups = true);

  void begin();  // configure les GPIO

  // Offset Nord en degrés (+ = sens horaire). Ex: si "N" physique tombe sur "E" (90°),
  // setNorthOffsetDegrees(-90) pour réaligner l’angle calculé à 0° sur le Nord réel.
  void setNorthOffsetDegrees(float offsetDeg);
  float getNorthOffsetDegrees() const { return _northOffset; }

  // Lecture non-bloquante avec petit debounce (delay interne).
  // Retourne true si un masque stable a été obtenu et interprété.
  bool update(uint16_t debounceMs = 10, uint8_t stableReads = 3);

  // Raccourcis "bloquants": lisent, interprètent et renvoient directement
  int8_t        readIndex(uint16_t windowMs = 20);           // 0..7 ou -1 si inconnu
  float         readAngle(uint16_t windowMs = 20);           // 0..360) ou NAN si inconnu
  const char*   readName(uint16_t windowMs = 20);            // "N", "NE", ... ou "UNKNOWN"

  // Accesseurs (basés sur la dernière update()/read*)
  int8_t        directionIndex() const { return _lastIndex; }
  Dir8          directionEnum() const;
  const char*   directionName() const;
  float         angleDegrees() const { return _lastAngle; }
  uint8_t       rawMask() const { return _lastMask; }        // 8 bits: bit i = direction active

private:
  uint8_t _pins[NUM_SWITCHES];
  bool    _invert;
  bool    _usePullups;
  float   _northOffset = 0.0f;

  uint8_t _lastMask = 0;
  int8_t  _lastIndex = -1;       // 0..7 ou -1
  float   _lastAngle = NAN;      // degrés ; NAN si inconnu

  uint8_t readMaskOnce() const;
  static bool   isSingleBit(uint8_t m);
  static uint8_t popcount8(uint8_t m);
  static bool   areAdjacentBits(uint8_t m, uint8_t& lowIdx, uint8_t& highIdx);

  void computeFromMask(uint8_t m);
  static float wrap360(float deg);
};

#endif // GIROUETTE_H
