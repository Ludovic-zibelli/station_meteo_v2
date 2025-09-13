#pragma once
#include <Arduino.h>

constexpr uint8_t NUM_SWITCHES = 8;

enum class Dir8 : int8_t { N=0, NE, E, SE, S, SW, W, NW, UNKNOWN=-1 };

class Girouette {
public:
  enum class Health : uint8_t { OK=0, NO_ACTIVE, MULTIPLE_ACTIVE, UNSTABLE, SELF_TEST_RUNNING, NOT_INITIALIZED };

  Girouette(const uint8_t pins[NUM_SWITCHES], bool invertLogic=true, bool usePullups=true);
  void begin();

  // Lecture / calcul
  bool update(uint16_t debounceMs=8, uint8_t stableReads=3);
  int8_t  readIndex(uint16_t windowMs=15);
  float   readAngle(uint16_t windowMs=15);
  const char* readName(uint16_t windowMs=15);

  Dir8 directionEnum() const;
  const char* directionName() const;

  // Calibration
  void  setNorthOffsetDegrees(float offsetDeg);
  float getNorthOffsetDegrees() const { return _northOffset; }
  bool  calibrateNorthToCurrent(); // retourne true si la calibration a été effectuée

  // Bouton de calibration Nord (appui long)
  void enableNorthButton(int pin, bool activeLow=true, uint16_t debounceMs=30, uint16_t longPressMs=800);
  void disableNorthButton();
  bool calibrationJustPerformed(); // true une fois après déclenchement

  // Contrôle de fonctionnement
  Health  quickHealth() const { return _health; }
  uint8_t lastRawMask() const { return _lastMask; }

  // Auto-test (diagnostic)
  void     startSelfTest(uint32_t durationMs=10000);
  bool     selfTestRunning() const { return _selfTest; }
  bool     selfTestFinished() const { return _selfFinished; }
  uint8_t  selfTestSeenMask() const { return _seenMask; }
  uint8_t  selfTestMissingMask() const { return uint8_t(~_seenMask) & 0xFF; }
  bool     selfTestOk() const { return (_seenMask & 0xFFu) == 0xFFu; }

private:
  // Helpers
  uint8_t readMaskOnce() const;
  static bool     isSingleBit(uint8_t m);
  static uint8_t  popcount8(uint8_t m);
  static bool     areAdjacentBits(uint8_t m, uint8_t& lowIdx, uint8_t& highIdx);
  static float    wrap360(float deg);
  void            computeFromMask(uint8_t m);
  void            pollButton();

  // Pins / config
  uint8_t _pins[NUM_SWITCHES];
  bool    _invert;
  bool    _usePullups;

  // État courant
  uint8_t _lastMask{0};
  int8_t  _lastIndex{-1};
  float   _lastAngle{NAN};
  float   _northOffset{0.0f};
  Health  _health{Health::NOT_INITIALIZED};

  // Bouton Nord
  int      _btnPin{-1};
  bool     _btnActiveLow{true};
  bool     _btnEnabled{false};
  uint16_t _btnDebounceMs{30};
  uint16_t _btnLongMs{800};
  bool     _btnLast{false};
  uint32_t _btnChangeMs{0};
  bool     _btnLongFired{false};
  bool     _calibJustDone{false};

  // Auto-test
  bool     _selfTest{false};
  uint32_t _selfStart{0};
  uint32_t _selfDur{0};
  bool     _selfFinished{false};
  uint8_t  _seenMask{0};
};

