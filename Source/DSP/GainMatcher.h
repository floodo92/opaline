#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

/* ============================================================
   GAINMATCHER — compensación automática de volumen.
   Mantiene el loudness de salida igualado al de entrada para que
   subir el knob Shine se oiga como "más brillo/densidad", NO como
   "más fuerte" (A/B honesto). Rastrea RMS in/out con smoothing lento
   y aplica una ganancia complementaria acotada a ±6 dB.
   ============================================================ */
class GainMatcher
{
public:
    void prepare (double sr, int blockSize);

    /* Llama CADA bloque con el RMS medio (lineal) de entrada y salida.
       Devuelve la ganancia (lineal) a multiplicar al buffer. */
    float process (float inputRms, float outputRms);

    void setEnabled (bool e)  { enabled = e; if (!e) { trackedIn = 0; trackedOut = 0; gain = 1.0f; } }
    bool isEnabled() const noexcept { return enabled; }
    float getGainDb() const noexcept { return 20.0f * std::log10 (juce::jmax (1e-4f, gain)); }

private:
    bool enabled = true;
    float trackedIn = 0.0f, trackedOut = 0.0f;
    float gain = 1.0f;
    /* Coeficientes por bloque calculados en prepare() a partir de la duración
       real del bloque (blockSize/sr) para que el smoothing sea consistente
       en cualquier tamaño de buffer. */
    double coeffIn = 0.02, coeffOut = 0.02, gainCoeff = 0.01;
};
