// Bypass-toggle test: reproduce EXACTO el bucle de OpalineAudioProcessor::processBlock
// (ChainVoice + GainMatcher + output trim + bypass que no llama a la cadena) y mide
// si el tilt espectral (peso bajo vs agudos) deriva con cada ciclo de bypass.
#include "../../Source/DSP/VoiceChain.h"
#include "../../Source/DSP/GainMatcher.h"
#include <cstdio>
#include <vector>
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static std::vector<float> makeVocal (double sr, int nSamples)
{
    // Entrada ESTACIONARIA (estadística constante en el tiempo): ruido filtrado
    // = cuerpo denso + agudos. Si la cadena deriva con esto, hay un integrador
    // interno de largo plazo (no un artefacto del patrón sintético).
    std::vector<float> v (nSamples);
    float lpState = 0.0f;   // cuerpo: ruido low-passed ~500 Hz (one-pole)
    double lpCoef = 1.0 - std::exp (-2.0 * M_PI * 500.0 / sr);
    for (int i = 0; i < nSamples; ++i)
    {
        const float w = (std::rand() / (float) RAND_MAX * 2.0f - 1.0f);
        lpState += (float) lpCoef * (w - lpState);
        v[i] = lpState * 1.8f + 0.10f * w;   // + sibilancia (ruido blanco atenuado)
    }
    return v;
}

struct Bands
{
    juce::dsp::IIR::Filter<float> low, hi;
    void prepare (double sr, int block)
    {
        auto spec = juce::dsp::ProcessSpec { sr, (juce::uint32) block, 1 };
        low.prepare (spec); hi.prepare (spec);
        low.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, 250.0f, 0.6f);  // "peso" ~100-600 Hz
        hi.coefficients  = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 4000.0f, 0.707f);
        low.reset(); hi.reset();
    }
    void measure (const std::vector<float>& buf, float& lowR, float& hiR)
    {
        float la = 0.0f, ha = 0.0f;
        for (float x : buf)
        {
            float l = low.processSample (x); la += l * l;
            float h = hi.processSample (x);  ha += h * h;
        }
        lowR = std::sqrt (la / (float) buf.size());
        hiR  = std::sqrt (ha / (float) buf.size());
    }
};

int main (int argc, char** argv)
{
    const double sr = 48000.0;
    const int block = 512;
    const int wetBlocks = argc > 1 ? std::atoi (argv[1]) : 120;
    const int dryBlocks = argc > 2 ? std::atoi (argv[2]) : 0;
    const char* mode = argc > 3 ? argv[3] : "full";
    const int enough = 40;
    const int cycles = 12;

    const std::vector<float> vocal = makeVocal (sr, (size_t) (wetBlocks + dryBlocks) * cycles * block);

    // Sonda: filtro high-pass aislado (el del exciter) con entrada estacionaria.
    if (juce::String (mode) == "hp")
    {
        juce::dsp::IIR::Filter<float> hp;
        hp.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 1 });
        hp.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 3000.0f, 0.707f);
        hp.reset();
        printf ("== HP-AISLADO (makeHighPass 3k, Q0.707, entrada estacionaria) ==\n");
        auto* cf = hp.coefficients.get();
        printf ("  b0=%.8f b1=%.8f b2=%.8f a1=%.8f a2=%.8f\n",
                cf->coefficients[0], cf->coefficients[1], cf->coefficients[2],
                cf->coefficients[3], cf->coefficients[4]);
        for (int cyc = 0; cyc < 12; ++cyc)
        {
            float acc = 0.0f; int n = 0;
            float oacc = 0.0f;
            const float drive = 3.2f, mixm = 0.385f, bias = 0.16f;
            for (int i = 0; i < (wetBlocks - enough) * block; ++i)
            {
                const float x = vocal[(size_t) cyc * wetBlocks * block + i];
                float y = hp.processSample (x);
                acc += y * y; ++n;
                float sat = std::tanh (y * drive + bias) - std::tanh (bias);
                float o = x * (1.0f - mixm) + sat * mixm;
                oacc += o * o;
            }
            printf ("  win%2d  hpRms=%.6f  excOutRms=%.6f\n",
                    cyc, std::sqrt (acc / (float) n), std::sqrt (oacc / (float) n));
        }
        return 0;
    }

    LearnProfile prof;
    prof.valid = true;
    prof.sibilanceLevel = (juce::String (mode) == "full" || juce::String (mode) == "nosup") ? 0.6f : 0.0f;
    prof.resonanceLevel = (juce::String (mode) == "full" || juce::String (mode) == "nodess") ? 0.6f : 0.0f;
    prof.lowMidLevel    = 0.5f;
    prof.brightness     = 0.5f;
    prof.airTargetDb    = 7.0f;
    prof.sibExcessDb    = 6.0f;
    prof.deEssFreq      = 7500.0f;

    printf ("== OPALINE BYPASS (bucle exacto del processor) ==\n");
    printf ("modo=%s wet=%d dry=%d blocks, cycles=%d (block=%d @ %.0f Hz)\n",
            mode, wetBlocks, dryBlocks, cycles, block, sr);

    VoiceChain chain;
    chain.prepare (sr, block, 1);
    chain.setLearnProfile (prof);
    GainMatcher gm;
    gm.prepare (sr, block);
    const bool useChain = (juce::String (mode) != "mat");
    const bool useGm    = (juce::String (mode) != "nogm");
    const float amount  = (juce::String (mode) == "eqonly") ? 0.0f : 1.0f;
    const float bodyAir = 0.0f, outDb = 1.2f;

    // Modos de aislamiento por etapa (máscara de procesado).
    unsigned int mask = 0xFFFFFFFFu;
    if (juce::String (mode) == "eqonly")      mask = VoiceChain::kStageEq;
    if (juce::String (mode) == "noopt")       mask &= ~VoiceChain::kStageOptical;
    if (juce::String (mode) == "noexc")       mask &= ~VoiceChain::kStageExciter;
    if (juce::String (mode) == "nopar")       mask &= ~VoiceChain::kStageParallel;
    if (juce::String (mode) == "opt")         mask = VoiceChain::kStageOptical;
    if (juce::String (mode) == "exc")         mask = VoiceChain::kStageExciter;
    if (juce::String (mode) == "par")         mask = VoiceChain::kStageParallel;
    if (juce::String (mode) == "pe")          mask = VoiceChain::kStageOptical | VoiceChain::kStageExciter;
    if (juce::String (mode) == "both0")       mask &= ~(VoiceChain::kStageDeEsser | VoiceChain::kStageSuppressor);
    if (juce::String (mode) == "nodess")      mask &= ~VoiceChain::kStageDeEsser;
    if (juce::String (mode) == "nosup")       mask &= ~VoiceChain::kStageSuppressor;

    juce::AudioBuffer<float> buf ((size_t) 1, block);
    Bands bands; bands.prepare (sr, block);
    int bypassed = 0;
    size_t pos = 0;
    float refLow = 0.0f, refHi = 0.0f, refTilt = 0.0f;

    // El plugin real fuerza el amount en prepareToPlay (setAmount(..., true)).
    const bool forceWarm = argc > 4 && juce::String (argv[4]) == "force";
    if (forceWarm) { chain.setAmount (amount, true); chain.setBodyAir (bodyAir); }

    for (int cyc = 0; cyc < cycles; ++cyc)
    {
        // Segmento WET: todo el bucle que hace el processor cuando NO está en bypass.
        std::vector<float> wetAcc;
        wetAcc.reserve ((size_t) (wetBlocks - enough) * block);
        std::vector<float> rawAcc;
        rawAcc.reserve (wetAcc.capacity());
        const bool excloop = (juce::String (mode) == "excloop");
        for (int b = 0; b < wetBlocks; ++b)
        {
            auto* d = buf.getWritePointer (0);
            const size_t src = excloop ? ((size_t) b * block) : pos;
            for (int i = 0; i < block; ++i)
                d[i] = vocal[src + i];
            if (b >= enough)
                rawAcc.insert (rawAcc.end(), d, d + block);
            if (!excloop) pos += (size_t) block;

            float rmsIn = 0.0f;
            for (int i = 0; i < block; ++i) { float s = d[i]; rmsIn += s * s; }
            rmsIn = std::sqrt (rmsIn / (float) block);

            if (!bypassed && useChain)
            {
                chain.setAmount (amount);
                chain.setBodyAir (bodyAir);
                chain.process (buf, mask);
                buf.applyGain (juce::Decibels::decibelsToGain (outDb));
                float rmsOut = 0.0f;
                for (int i = 0; i < block; ++i) { float s = d[i]; rmsOut += s * s; }
                rmsOut = std::sqrt (rmsOut / (float) block);
                if (useGm)
                    buf.applyGain (gm.process (rmsIn, rmsOut));
                if (b >= enough)
                    wetAcc.insert (wetAcc.end(), d, d + block);
            }
            else if (b >= enough)
                wetAcc.insert (wetAcc.end(), d, d + block);
        }

        float lr, hr, ilr, ihr;
        bands.measure (wetAcc, lr, hr);
        bands.measure (rawAcc, ilr, ihr);
        float tilt = 20.0f * std::log10 (juce::jmax (1e-6f, hr) / juce::jmax (1e-6f, lr));
        if (cyc == 0) { refLow = lr; refHi = hr; refTilt = tilt; }
        printf ("%3d  low=%7.4f(%+5.1f)  hi=%7.4f(%+5.2f)  tilt=%+6.2f(%+6.2f)\n",
                cyc, lr, 20.0f * std::log10 (juce::jmax (1e-6f, lr) / juce::jmax (1e-6f, refLow)),
                hr, 20.0f * std::log10 (juce::jmax (1e-6f, hr) / juce::jmax (1e-6f, refHi)),
                tilt, tilt - refTilt);
        printf ("     (in  low=%7.4f hi=%7.4f)\n", ilr, ihr);

        // Segmento DRY (apagado): la cadena NO se procesa; solo avanza la señal.
        for (int b = 0; b < dryBlocks; ++b) pos += (size_t) block;
    }
    printf ("\nSi (dL,dH,tilt) se quedan en ~0 y la columna dTilt no sube con cada\n");
    printf ("toggle -> el motor no deriva; el 'peso' creciente es una percepcion del\n");
    printf ("contraste wet/dry o algo del host, no del DSP.\n");
    return 0;
}