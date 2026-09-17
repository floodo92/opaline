#include "LearnEngine.h"
#include <algorithm>
#include <cmath>
#include <cstring>

/* ============================================================
   LEARNENGINE — análisis de la take para calibrar el perfil.
   Método: STFT streaming con búfer interno (le roba al bloque del
   host lo que mida, así funciona para bloques 128..2048+), energía
   por bandas, centro = bin de máxima magnitud, sibilancia por
   percentil del exceso transitorio y pitch por autocorrelación.
   Todo pre-dimensionado: cero allocs en el hilo de audio.
   ============================================================ */

namespace
{
/* Inserta en un array top-N ASCENDENTE (el índice 0 guarda el más chico;
   los topes de gama se descartan al llenarse). Sin heap: solo float*. */
void insertTop (float v, float* arr, int& count, int cap)
{
    if (count < cap)     { arr[count++] = v; }
    else if (v > arr[0]) { arr[0] = v; }
    else                 { return; }
    std::sort (arr, arr + count);
}

/* Percentil `p` (0..1) de un top-N (robusto a un golpe aislado). */
static float percentile (const float* arr, int count, float p)
{
    if (count <= 0) return 0.0f;
    const int idx = juce::jlimit (0, count - 1, (int) (p * (float) (count - 1) + 0.5f));
    return arr[idx];
}
}

void LearnEngine::prepare (double sr, int maxBlockSize)
{
    sampleRate = sr;
    maxBlock  = maxBlockSize;
    accumSamples.store (0);
    analyzedWindows.store (0);
    targetSamples.store ((int) (sampleRate * 4.0));   // ~4 segundos

    // Pre-dimensionado una sola vez (hilo de control): NUNCA allocs en audio.
    window.resize (kFftSize);
    fftData.resize (2 * kFftSize);
    mono.resize (kFftSize);
    monoBuf.resize ((size_t) maxBlock + kFftSize + kHop);
    monoCount = 0;

    // Ventana Hann precomputada.
    for (int i = 0; i < kFftSize; ++i)
        window[i] = (float) (0.5 * (1.0 - std::cos (2.0 * juce::MathConstants<double>::pi * i / (kFftSize - 1))));
}

void LearnEngine::setMeasuring (bool m)
{
    measuring.store (m, std::memory_order_relaxed);
    accumSamples.store (0);
    if (m)
        pendingReset.store (true, std::memory_order_release);   // el reset de `b` y del ring lo hace el audio
}

float LearnEngine::getProgress() const noexcept
{
    const int target = targetSamples.load (std::memory_order_relaxed);
    if (target <= 0) return 0.0f;
    return juce::jlimit (0.0f, 1.0f,
                         (float) accumSamples.load (std::memory_order_relaxed) / (float) target);
}

void LearnEngine::process (const juce::AudioBuffer<float>& buffer, bool active)
{
    if (!active || !measuring.load (std::memory_order_relaxed))
        return;

    // Reset de acumuladores solicitado por la UI: se ejecuta aquí (hilo de audio).
    if (pendingReset.exchange (false, std::memory_order_acquire))
    {
        b = Buckets {};
        monoCount = 0;
        quietWindows = 0;
    }

    const int n = buffer.getNumSamples();
    const int ch = juce::jmin (2, buffer.getNumChannels());
    if (n <= 0 || ch <= 0) return;

    float* base = monoBuf.data();
    int total = monoCount + n;

    // Salvaguarda anti-desborde (contract: n <= maxBlock; aquí por si el host
    // entrega de golpe más de lo preparado). Recorta del frente sin alloc.
    if (total > (int) monoBuf.size())
    {
        const int drop = juce::jmax (kHop, total - (int) monoBuf.size() + kHop);
        if (drop < monoCount)
        {
            std::memmove (base, base + drop, (size_t) (monoCount - drop) * sizeof (float));
            monoCount -= drop;
            total = monoCount + n;
        }
    }
    if (total > (int) monoBuf.size()) return;   // irrecuperable, se descarta el bloque

    // Mezcla a mono y encola.
    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f;
        for (int c = 0; c < ch; ++c)
            s += buffer.getSample (c, i);
        base[monoCount + i] = s / (float) ch;
    }
    monoCount = total;

    const double binHz = sampleRate / (double) kFftSize;
    const double bodyBinsF = (1000.0 - 500.0) / binHz;

    // Drena ventanas completas (hop = 512), igual que con buffers grandes.
    while (monoCount >= kFftSize)
    {
        for (int i = 0; i < kFftSize; ++i)
            fftData[i] = base[i] * window[i];
        for (int i = kFftSize; i < 2 * kFftSize; ++i)
            fftData[i] = 0.0f;

        fft.performFrequencyOnlyForwardTransform (fftData.data(), true);   // magnitudes

        double peakSibMag = 0.0, peakSibFreq = 0.0;
        double peakResMag = 0.0, peakResFreq = 0.0;
        double frameBody = 0.0;

        for (int k = 1; k <= kFftSize / 2; ++k)
        {
            const double freq = k * binHz;
            const double mag = fftData[k];
            const double e   = mag * mag;

            if (freq >= 5000 && freq <= 10000)
            {
                b.sib += e;
                if (mag > peakSibMag) { peakSibMag = mag; peakSibFreq = freq; }
            }
            else if (freq >= 2000 && freq < 5000)
            {
                b.res += e;
                if (mag > peakResMag) { peakResMag = mag; peakResFreq = freq; }
            }
            if (freq >= 200 && freq < 500)       b.lowMid += e;
            if (freq >= 500 && freq < 1000)      { b.body += e; frameBody += e; }
            if (freq >= 2000 && freq < 4000)     b.pres += e;
            b.total += e;
        }

        // Acumula centro (ponderado por magnitud de pico).
        if (peakSibMag > 0.0) { b.sibFreqAcc += peakSibFreq * peakSibMag; b.peakSib += peakSibMag; }
        if (peakResMag > 0.0) { b.resFreqAcc += peakResFreq * peakResMag; b.peakRes += peakResMag; }

        // Sibilancia TRANSIENTE: en cada ventana, el PICO de la banda 5-10k vs el nivel
        // del cuerpo (por-bin medio) EN EL MISMO FRAME. El promedio ancho nunca
        // excede el cuerpo; el pico de la "S" sí manda. Se guarda el exceso por
        // ventana en un top-N y se toma el percentil p60 (las colas de percusión
        // puntual no disparan la medición, pero las S recurrentes sí).
        if (peakSibMag > 0.0 && bodyBinsF > 1.0 && frameBody > 0.0)
        {
            const double bodyMagF = std::sqrt (frameBody / bodyBinsF);
            if (bodyMagF > 1e-4)
            {
                const float excessDb = (float) (20.0 * std::log10 (peakSibMag)
                                              - 20.0 * std::log10 (bodyMagF));
                insertTop (excessDb, b.excTop, b.excCount, 32);
            }
        }

        // Avanza hop (el frente del ring se descarta con memmove, sin alloc).
        std::memmove (base, base + kHop, (size_t) (monoCount - kHop) * sizeof (float));
        monoCount -= kHop;
        analyzedWindows.fetch_add (1, std::memory_order_relaxed);

        // Espera VOZ REAL: el progreso solo avanza cuando esta ventana trae
        // cuerpo (500-1k) por encima de un piso. Si se pulsa Measure sin que
        // entre voz, el Learn NO "completa" en silencio (sin progreso fantasma
        // a 100 % con perfil inválido): se queda escuchando hasta acumular 4 s
        // de voz de verdad. El usuario cancela pulsando el botón (toggle).
        if (frameBody > kBodyFloor)
        {
            accumSamples.fetch_add (kHop, std::memory_order_relaxed);
            quietWindows = 0;
        }
    }

    // Pitch por autocorrelación (primeras muestras del bloque del host).
    const int pn = juce::jmin (n, kFftSize);
    if (pn >= 256)
    {
        for (int i = 0; i < pn; ++i)
        {
            float s = 0.0f;
            for (int c = 0; c < ch; ++c)
                s += buffer.getSample (c, i);
            mono[i] = s / (float) ch;
        }
        const float f = autocorrelate (mono.data(), pn, sampleRate);
        if (f > 60.0f && f < 1200.0f) { b.pitchSum += f; b.pitchCount += 1.0; }
    }
}

float LearnEngine::autocorrelate (const float* x, int n, double sr)
{
    const int lagMin = juce::jmax (1, (int) (sr / 1200.0));
    const int lagMax = (int) (sr / 60.0);
    if (lagMax >= n) return 0.0f;

    float best = 0.0f;
    int bestLag = 0;
    for (int lag = lagMin; lag <= lagMax && lag < n; ++lag)
    {
        float sum = 0.0f, e0 = 0.0f, e1 = 0.0f;
        const int lim = n - lag;
        for (int i = 0; i < lim; ++i)
        {
            const float a = x[i], b = x[i + lag];
            sum += a * b; e0 += a * a; e1 += b * b;
        }
        const float den = std::sqrt (e0 * e1) + 1e-6f;
        const float corr = sum / den;
        if (corr > best) { best = corr; bestLag = lag; }
    }
    if (bestLag <= 0) return 0.0f;
    return (float) (sr / (double) bestLag);
}

LearnProfile LearnEngine::finish()
{
    LearnProfile p;
    measuring.store (false, std::memory_order_relaxed);
    accumSamples.store (0);

    if (b.total <= 0.0)
    {
        lastReason.store (2, std::memory_order_relaxed);
        lastBodyEnergy.store (b.body, std::memory_order_relaxed);
        lastSibEnergy.store (b.sib, std::memory_order_relaxed);
        lastWindows.store (analyzedWindows.load (std::memory_order_relaxed),
                           std::memory_order_relaxed);
        b = Buckets {};
        p.valid = false;
        return p;
    }
    if (b.peakSib <= 0.0 && b.peakRes <= 0.0)
    {
        lastReason.store (3, std::memory_order_relaxed);
        lastBodyEnergy.store (b.body, std::memory_order_relaxed);
        lastSibEnergy.store (b.sib, std::memory_order_relaxed);
        lastWindows.store (analyzedWindows.load (std::memory_order_relaxed),
                           std::memory_order_relaxed);
        b = Buckets {};
        p.valid = false;
        return p;
    }

    auto soft = [](double r, double scale) {
        return (float) juce::jlimit (0.0, 1.0, r * scale);
    };

    p.sibilanceLevel = soft (b.sib    / (b.total + 1e-12), 14.0);
    p.resonanceLevel = soft (b.res    / (b.total + 1e-12), 12.0);
    p.lowMidLevel    = soft (b.lowMid / (b.total + 1e-12), 7.0);
    p.brightness     = soft (b.pres   / (b.total + 1e-12), 10.0);
    p.dynamics       = soft (b.sib    / (b.total + 1e-12), 20.0);

    // Centro de sibilancia: frecuencia media ponderada por pico acumulado.
    if (b.peakSib > 0.0)
        p.deEssFreq = (float) juce::jlimit (5200.0, 9500.0, b.sibFreqAcc / b.peakSib);
    else
        p.deEssFreq = 6800.0f;

    if (b.peakRes > 0.0)
        p.resonanceFreq = (float) juce::jlimit (2200.0, 4800.0, b.resFreqAcc / b.peakRes);
    else
        p.resonanceFreq = 3400.0f;

    p.lowMidFreq = 320.0f;
    p.airFreq    = 12000.0f;
    p.presenceFreq = 2500.0f;

    // === Pendiente espectral objetivo ===
    // Criterio (triángulo de Paco Rincón): los AGUDOS (presencia 2-4k) de una voz de
    // estudio están típicamente ~14 dB por debajo del cuerpo (500-1k) por-bin; la
    // pendiente ideal se pone ahí. airTargetDb ≈ 0 -> ya equilibrada (no tocar,
    // el aire lo da el shelf fijo); positivo -> sube presencia; negativo leve ->
    // la suaviza si sobra. El clamp a -3 evita vaciar el brillo.
    {
        const double bodyBins = juce::jmax (1.0, (1000.0 - 500.0) / (sampleRate / kFftSize));
        const double presBins = juce::jmax (1.0, (4000.0 - 2000.0) / (sampleRate / kFftSize));
        const double bodyMag  = std::sqrt (b.body / bodyBins);
        const double presMag  = std::sqrt (b.pres / presBins);
        const double bodyDb   = 20.0 * std::log10 (bodyMag + 1e-9);
        const double presDb   = 20.0 * std::log10 (presMag  + 1e-9);
        const double target   = 14.0;
        p.airTargetDb = (float) juce::jlimit (-3.0, 15.0, (bodyDb - target) - presDb);
        // Brightness ligado a la dirección del ajuste (0..1) para la UI.
        p.brightness = soft (juce::jmax (0.0, (double) p.airTargetDb / 12.0), 1.0);

        // === Regla de sibilancias (Paco Rincón) ===
        // Las "S" (5-10k) NUNCA deben superar el nivel del cuerpo (500-1k).
        // Exceso = percentil 60 del pico-de-sibilancia vs cuerpo (mismo frame).
        p.sibExcessDb = (float) juce::jlimit (0.0f, 24.0f,
                                              percentile (b.excTop, b.excCount, 0.60f));
    }

    if (b.pitchCount > 0.0)
        p.medianPitchHz = (float) (b.pitchSum / b.pitchCount);

    lastReason.store (1, std::memory_order_relaxed);
    lastBodyEnergy.store (b.body, std::memory_order_relaxed);
    lastSibEnergy.store (b.sib, std::memory_order_relaxed);
    lastWindows.store (analyzedWindows.load (std::memory_order_relaxed),
                       std::memory_order_relaxed);
    b = Buckets {};
    p.valid = true;
    return p;
}