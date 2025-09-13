#include "girouette.h"
#include <math.h>

static const char* DIR8_NAMES[8] = {
  "N", "NE", "E", "SE", "S", "SW", "W", "NW"
};

Girouette::Girouette(const uint8_t pins[NUM_SWITCHES], bool invertLogic, bool usePullups)
: _invert(invertLogic), _usePullups(usePullups) {
  for (uint8_t i = 0; i < NUM_SWITCHES; ++i) {
    _pins[i] = pins[i];
  }
}

void Girouette::begin() {
  for (uint8_t i = 0; i < NUM_SWITCHES; ++i) {
    if (_usePullups) {
      pinMode(_pins[i], INPUT_PULLUP);
    } else {
      pinMode(_pins[i], INPUT);
    }
  }
  if (_btnEnabled && _btnPin >= 0) {
    pinMode(_btnPin, _btnActiveLow ? INPUT_PULLUP : INPUT);
    _btnLast = false;
    _btnChangeMs = millis();
    _btnLongFired = false;
    _calibJustDone = false;
  }
  _health = Health::NOT_INITIALIZED;
  // Lecture initiale
  update();
}

void Girouette::setNorthOffsetDegrees(float offsetDeg) {
  _northOffset = fmodf(offsetDeg, 360.0f);
  if (_northOffset < 0) _northOffset += 360.0f;
}

bool Girouette::update(uint16_t debounceMs, uint8_t stableReads) {
  if (stableReads < 1) stableReads = 1;

  uint8_t last = readMaskOnce();
  uint8_t stableCount = 1;

  // Essaie d'obtenir le même masque "stableReads" fois d'affilée
  while (stableCount < stableReads) {
    delay(debounceMs);
    pollButton(); // échantillonne le bouton pendant l'attente
    uint8_t cur = readMaskOnce();
    if (cur == last) {
      ++stableCount;
    } else {
      last = cur;
      stableCount = 1;
    }
  }

  _lastMask = last;
  computeFromMask(last);

  // Auto-test : accumule les positions vues uniquement pour états plausibles
  if (_selfTest) {
    // On accepte 1 bit ou 2 bits adjacents
    uint8_t lowI=0, highI=0;
    if (isSingleBit(last) || areAdjacentBits(last, lowI, highI)) {
      _seenMask |= last;
    }
    if ((millis() - _selfStart) >= _selfDur) {
      _selfTest = false;
      _selfFinished = true;
    }
  }

  // Quand l'auto-test est en cours, annonce l'état spécifique
  if (_selfTest) {
    _health = Health::SELF_TEST_RUNNING;
  }

  // Bouton (détection appui long -> calibration)
  pollButton();

  return !isnan(_lastAngle) || _lastIndex >= 0;
}

int8_t Girouette::readIndex(uint16_t windowMs) {
  update(windowMs / 3u + 1u, 3);
  return _lastIndex;
}

float Girouette::readAngle(uint16_t windowMs) {
  update(windowMs / 3u + 1u, 3);
  return _lastAngle;
}

const char* Girouette::readName(uint16_t windowMs) {
  update(windowMs / 3u + 1u, 3);
  return directionName();
}

Dir8 Girouette::directionEnum() const {
  if (_lastIndex < 0) return Dir8::UNKNOWN;
  return static_cast<Dir8>(_lastIndex % 8);
}

const char* Girouette::directionName() const {
  if (_lastIndex < 0) return "UNKNOWN";
  return DIR8_NAMES[_lastIndex % 8];
}

uint8_t Girouette::readMaskOnce() const {
  uint8_t mask = 0;
  for (uint8_t i = 0; i < NUM_SWITCHES; ++i) {
    int v = digitalRead(_pins[i]);
    bool active = _invert ? (v == LOW) : (v == HIGH);
    if (active) {
      mask |= (1u << i);
    }
  }
  return mask;
}

bool Girouette::isSingleBit(uint8_t m) {
  return m && ((m & (m - 1)) == 0);
}

uint8_t Girouette::popcount8(uint8_t m) {
  // Petit popcount portable
  m = (m & 0x55) + ((m >> 1) & 0x55);
  m = (m & 0x33) + ((m >> 2) & 0x33);
  m = (m + (m >> 4)) & 0x0F;
  return m;
}

bool Girouette::areAdjacentBits(uint8_t m, uint8_t& lowIdx, uint8_t& highIdx) {
  if (popcount8(m) != 2) return false;
  int8_t first = -1, second = -1;
  for (uint8_t i = 0; i < NUM_SWITCHES; ++i) {
    if (m & (1u << i)) {
      if (first < 0) first = i;
      else { second = i; break; }
    }
  }
  if (first < 0 || second < 0) return false;
  // Ordonne
  if (first > second) { int8_t tmp = first; first = second; second = tmp; }
  // Adjacent direct (ex: 2 et 3) ou adjacence circulaire (0 et 7)
  if ((second - first) == 1) {
    lowIdx = first; highIdx = second; return true;
  }
  if (first == 0 && second == 7) {
    lowIdx = 7; highIdx = 0; return true;
  }
  return false;
}

float Girouette::wrap360(float deg) {
  deg = fmodf(deg, 360.0f);
  if (deg < 0) deg += 360.0f;
  return deg;
}

void Girouette::computeFromMask(uint8_t m) {
  // Valeurs par défaut
  _lastIndex = -1;
  _lastAngle = NAN;

  if (m == 0) {
    _health = Health::NO_ACTIVE;
    return;
  }

  // Cas 1: un seul bit => direction claire (N/NE/E/...)
  if (isSingleBit(m)) {
    uint8_t idx = 0;
    while (((m >> idx) & 0x1u) == 0u) ++idx;
    _lastIndex = idx;
    float base = idx * 45.0f;                  // 0,45,90,...
    _lastAngle = wrap360(base + _northOffset); // applique l'offset Nord
    _health = Health::OK;
    return;
  }

  // Cas 2: deux bits adjacents => moyenne circulaire (milieu)
  uint8_t lowI = 0, highI = 0;
  if (areAdjacentBits(m, lowI, highI)) {
    float a1 = lowI  * 45.0f;
    float a2 = highI * 45.0f;

    // Moyenne vectorielle (circulaire)
    float r1 = a1 * DEG_TO_RAD;
    float r2 = a2 * DEG_TO_RAD;
    float x = cosf(r1) + cosf(r2);
    float y = sinf(r1) + sinf(r2);
    float mean = atan2f(y, x) * RAD_TO_DEG;  // peut être négatif
    mean = wrap360(mean);

    // Applique offset Nord
    float angle = wrap360(mean + _northOffset);
    _lastAngle = angle;

    // Index "le plus proche" pour nom/enum
    int idx = (int)floorf((angle + 22.5f) / 45.0f) % 8;
    if (idx < 0) idx += 8;
    _lastIndex = idx;

    _health = Health::OK;
    return;
  }

  // Cas 3: motifs ambigus => anormal
  _health = Health::MULTIPLE_ACTIVE;
}

void Girouette::enableNorthButton(int pin, bool activeLow, uint16_t debounceMs, uint16_t longPressMs) {
  _btnPin        = pin;
  _btnActiveLow  = activeLow;
  _btnDebounceMs = debounceMs;
  _btnLongMs     = longPressMs;
  _btnEnabled    = (pin >= 0);
}

void Girouette::pollButton() {
  if (!_btnEnabled || _btnPin < 0) return;

  int lv = digitalRead(_btnPin);
  bool pressed = _btnActiveLow ? (lv == LOW) : (lv == HIGH);
  unsigned long now = millis();

  // Anti-rebond : on valide un changement d’état si stable depuis _btnDebounceMs
  if (pressed != _btnLast) {
    if ((now - _btnChangeMs) >= _btnDebounceMs) {
      _btnLast = pressed;
      _btnChangeMs = now;

      if (_btnLast) {
        // Début d'appui : on réarme le long-press
        _btnLongFired = false;
      } else {
        // Relâchement : si le long press n'a pas été déclenché, c'était un appui court -> on ne fait rien
      }
    }
  } else {
    // Pas de changement d’état : si le bouton est maintenu, vérifier le long-press
    if (_btnLast && !_btnLongFired && (now - _btnChangeMs) >= _btnLongMs) {
      _btnLongFired = true;

      // Calibration : rendre l'angle courant égal à 0°
      // angle = wrap360(raw + _northOffset) => newOffset = wrap360(_northOffset - angle)
      if (!isnan(_lastAngle)) {
        _northOffset = wrap360(_northOffset - _lastAngle);
        _calibJustDone = true;
      }
    }
  }
}

