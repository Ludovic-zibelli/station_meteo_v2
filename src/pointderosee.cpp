#include "pointderosee.h"
#include <math.h>

float calculPointRosee(float T, float H) {
    // Constantes
    const float v1 = 0.061121;
    const float v2 = 17.67;
    const float v3 = 243.5;
    const float v4 = 440.8;
    const float v5 = 19.48;

    // Calcul
    float expPart = v1 * exp(v2 * T / (T + v3)) * H;
    float pt_rosee_dec = (v3 * log(expPart) - v4) / (v5 - log(expPart));

    return pt_rosee_dec; // °C
}
