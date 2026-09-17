#include "GainMatcher.h"
#include <cmath>

void GainMatcher::prepare (double sr, int blockSize)
{
    // Constantes de tiempo en duración real del bloque (blockSize/sr):
    // ~200 ms de smoothing del RMS y ~50 ms del gain aplicado, consistentes
    // para cualquier tamaño de buffer (128/256/512...).
    const double blockSec = (double) juce::jmax (1, blockSize) / sr;
    coeffIn  = 1.0 - std::exp (-blockSec / 0.2);
    coeffOut = coeffIn;
    gainCoeff = 1.0 - std::exp (-blockSec / 0.05);
    trackedIn = 0.0f;
    trackedOut = 0.0f;
    gain = 1.0f;
}

float GainMatcher::process (float inputRms, float outputRms)
{
    if (!enabled)
    {
        gain = 1.0f;
        return 1.0f;
    }

    // Blindaje: si cualquier medición es no-finito (NaN/inf), NO tocar el audio.
    if (! std::isfinite (inputRms) || ! std::isfinite (outputRms))
    {
        gain = 1.0f;
        return 1.0f;
    }

    // Smoothing de los RMS para evitar respuestas bruscas.
    trackedIn  += coeffIn  * (inputRms  - trackedIn);
    trackedOut += coeffOut * (outputRms - trackedOut);

    // Blindaje anti-"desmadre": si no hay señal real de entrada, NO tocar el
    // audio y NO acumular boost sobre silencio. Sin esto, el matcher empuja la
    // ganancia al máximo (+6dB) durante el bypass/reconexión y al reactivar la
    // señal golpea de golpe.
    if (trackedIn < 1e-4f)
    {
        gain = 1.0f;
        return 1.0f;
    }

    // Ganancia complementaria para llevar out → in, acotada ±6 dB.
    const float outSafe = juce::jmax (1e-5f, trackedOut);
    float target = trackedIn / outSafe;
    target = juce::jlimit (0.5f, 2.0f, target);   // ±6 dB

    // Smoothing de la ganancia aplicada (~50 ms).
    gain += (float) (gainCoeff * (target - gain));
    return gain;
}
