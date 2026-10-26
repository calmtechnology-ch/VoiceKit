#include "AudioResampler.h"

size_t AudioResampler::resample16to24(const int16_t* inSamples, size_t inCount,
                                     int16_t* outSamples, size_t outCapacity) {
    if (!inSamples || !outSamples || inCount == 0) {
        return 0;
    }

    // Le rapport d'échantillonnage 16 kHz -> 24 kHz est de 3/2 (1.5x)
    const size_t outCount = (inCount * 3) / 2;
    if (outCapacity < outCount) {
        return 0; // Capacité insuffisante
    }

    const size_t pairs = inCount / 2;
    for (size_t k = 0; k < pairs; ++k) {
        int32_t x0 = inSamples[2 * k];
        int32_t x1 = inSamples[2 * k + 1];
        int32_t x2;

        if (k + 1 < pairs) {
            x2 = inSamples[2 * k + 2];
        } else {
            // Extrapolation linéaire pour la frontière du dernier échantillon du bloc :
            // x2 = 2 * x1 - x0 (pente conservée)
            x2 = 2 * x1 - x0;
            if (x2 > 32767) x2 = 32767;
            if (x2 < -32768) x2 = -32768;
        }

        // Interpolation exacte pour les 3 échantillons de sortie :
        // y[0] à t = 0        : x0
        // y[1] à t = 2/3 Tin  : (x0 + 2*x1) / 3
        // y[2] à t = 4/3 Tin  : (2*x1 + x2) / 3
        int32_t y0 = x0;
        int32_t y1 = (x0 + 2 * x1 + ((x0 + 2 * x1 >= 0) ? 1 : -1)) / 3;
        int32_t y2 = (2 * x1 + x2 + ((2 * x1 + x2 >= 0) ? 1 : -1)) / 3;

        // Saturation bornée [-32768, 32767]
        if (y0 > 32767) y0 = 32767; else if (y0 < -32768) y0 = -32768;
        if (y1 > 32767) y1 = 32767; else if (y1 < -32768) y1 = -32768;
        if (y2 > 32767) y2 = 32767; else if (y2 < -32768) y2 = -32768;

        outSamples[3 * k + 0] = static_cast<int16_t>(y0);
        outSamples[3 * k + 1] = static_cast<int16_t>(y1);
        outSamples[3 * k + 2] = static_cast<int16_t>(y2);
    }

    return outCount;
}
