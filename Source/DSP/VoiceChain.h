#pragma once
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include "LearnEngine.h"

/* ============================================================
   VOICECHAIN — la cadena serial de brillo "sedoso/caro".
   Orden (mock de la cadena Dua Lipa, todo escalado por Shine/Amount):
     1  High-pass adaptativo (Learn, 40-150 Hz)
     2  Low-mid cut ~300 Hz (Learn)
     3  De-Esser 5-10 kHz (centro = Learn)
     4  Optical comp (LA-2A)
     5  Resonance suppressor 2-5k (el "sedoso", centro = Learn)
     6  Multiband lodos 250 Hz
     7  Harmonic exciter >2k
      8  Air shelf ~12k (el "precio")
      9  Parallel filtered bus (densidad sin lodo ni hiss)
     10  Tape glue final (A800 vocal: picos laminados + tercer armónico suave)
    ============================================================ */

class VoiceChain
{
public:
    VoiceChain() = default;

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    void setAmount (float amount, bool force = false);
    void setLearnProfile (const LearnProfile& p);
    const LearnProfile& getLearnProfile() const noexcept { return profile; }
    void setBodyAir (float v, bool force = false);

    void process (juce::AudioBuffer<float>& buffer, unsigned int stageMask = 0xFFFFFFFFu);

    float getCompGR() const noexcept { return compGR; }
    float getParGR()  const noexcept { return parGR; }

    /* Latencia real del chain (la aporta el oversampling del exciter). */
    int getLatencySamples() const noexcept { return exciter.getLatencySamples(); }

private:
    /* Envoltura mono de un polo (muestreo por muestra) para los detectores.
       Los coeficientes se precalculan en reset(sr, atk, rel) para no evaluar
       exp() por muestra (realtime). */
    struct EnvFollower
    {
        void reset (double sr, float atkMs, float relMs, bool stereo)
        {
            (void) stereo;
            sampleRate = sr;
            state = 0.0f;
            coeffAtk = (float) (1.0 - std::exp (-1.0 / (atkMs * 0.001 * sr + 1e-6)));
            coeffRel = (float) (1.0 - std::exp (-1.0 / (relMs * 0.001 * sr + 1e-6)));
        }
        void reset() { state = 0.0f; }

        float processSample (float in)
        {
            const float a = std::abs (in);
            const float coeff = (a > state) ? coeffAtk : coeffRel;
            state += coeff * (a - state);
            return state;
        }
        float getCurrentValue() const noexcept { return state; }

        double sampleRate = 48000.0;
        float coeffAtk = 1.0f, coeffRel = 0.001f, state = 0.0f;
    };

    /* 3: de-esser de techo relativo al cuerpo (regla Paco Rincón: la "S"
       nunca por encima del cuerpo 500-1k). Detector linked (max de canales),
       knee suave 2 dB, attack ~1 ms (sin look-ahead: cero latencia extra),
       release ~120 ms con gain persistente entre bloques (sin zipper en
       fronteras). Idle-bypass exacto: sin exceso de sibilancia, gain=1 y la
       mezcla seco-banda es paso perfecto. El centro lo calibra el Learn
       (profile.deEssFreq, histéresis de reconstrucción). */
    struct DeEsser
    {
        void prepare (double sr, int block, int nch);
        void reset();
        float process (juce::AudioBuffer<float>& b, float amount, float centre, float sibExcessDb);

        juce::dsp::IIR::Filter<float> bp[2];      // banda de sibilancia: un estado de filtro POR canal
        juce::dsp::IIR::Filter<float> bodyHP[2];  // detección de cuerpo: HP 500
        juce::dsp::IIR::Filter<float> bodyLP[2];  //                 + LP 1k -> banda 500-1k
        EnvFollower envSib;     // detector linked de sibilancia (max de canales)
        EnvFollower envBody;    // referencia linked de cuerpo (lenta: no la persigue la "S")
        float gainState = 1.0f; // gain suavizado que persiste entre bloques

        double sampleRate = 0; 
        int nch = 2;
        float lastCentre = -1.0f;
    };

    /* 4: compresor óptico estilo LA-2A — CASCADA de dos etapas:
       A "leveler" (densidad de cuerpo) + B "picos" (redondea lo que quedó).
       Mismo GR total que un comp profundo repartido en dos knees suaves
       -> densidad "on-air" sin bombeo. Ganancia persistente entre bloques. */
    struct OptoComp
    {
        void prepare (double sr, int block, int nch);
        void reset();
        float process (juce::AudioBuffer<float>& b, float amount);
        EnvFollower env;       // etapa A (lenta: 10/250)
        EnvFollower env2;      // etapa B (algo más rápida: 5/150)
        float gainA = 1.0f;    // estado de ganancia persistente por etapa
        float gainB = 1.0f;
        double sampleRate = 0; int nch = 2;
    };

    /* 5: supresor de resonancia (detección en banda + resta del anillo) */
    struct ResonanceSuppressor
    {
        void prepare (double sr, int block, int nch);
        void reset();
        float process (juce::AudioBuffer<float>& b, float amount, float centre);
        juce::dsp::IIR::Filter<float> detect[2], res[2];
        EnvFollower env;
        juce::LinearSmoothedValue<float> grSm { 0.0f };
        double sampleRate = 0; int nch = 2;
        float lastCentre = -1.0f;
    };

    /* 7: exciter armónico (tanh soft sobre >2k, oversampling 2× para
       minimizar aliasing de los armónicos generados) */
    struct HarmonicExciter
    {
        void prepare (double sr, int block, int nch);
        void reset();
        void process (juce::AudioBuffer<float>& b, float amount);
        int getLatencySamples() const noexcept
        { return os ? (int) os->getLatencyInSamples() : 0; }

        juce::dsp::IIR::Filter<float> hp;
        std::unique_ptr<juce::dsp::Oversampling<float>> os;
        double sampleRate = 0; int nch = 2;
    };

    /* 1/2/6/8: EQs biquad (hipass + low-mid + lodos + air shelf) */
    struct EqStage
    {
        void prepare (double sr, int block, int nch);
        void reset();
        void process (juce::AudioBuffer<float>& b, const LearnProfile& p,
                      float amount, float bodyAir);
        /* Peak filter RBJ correcto (workaround del makePeakFilter roto en JUCE 8.0.14). */
        static juce::dsp::IIR::Coefficients<float>::Ptr makePeak (double sr,
                                                                  float freq,
                                                                  float q,
                                                                  float gainDb);
        static juce::dsp::IIR::Coefficients<float>::Ptr makeHighShelf (double sr,
                                                                       float freq,
                                                                       float q,
                                                                       float gainDb);
        juce::dsp::IIR::Filter<float> highpass, lowMidCut, mudCut, airShelf, presence, presence4k;
        double sampleRate = 0; int nch = 2;
        /* Caché de coeficientes (histéresis): solo se reconstruye el filtro y se
            llama a reset() cuando la frecuencia/gain real cambian de forma
            significativa. Evita heap-alloc de Coefficients y el reset() por
            bloque que borra el estado (posibles clics en fronteras de bloque). */
        float lastHighpassF = 85.0f;
        float lastLowMidF = 320.0f,  lastLowMidG = 0.0f;
        float lastMudF    = 250.0f,  lastMudG    = 0.0f;
        float lastAirF    = 12000.0f, lastAirG   = 0.0f;
        float lastPresF   = 2500.0f,  lastPresG  = 0.0f;
        float lastPres4kG = 0.0f;
    };

    /* 9: paralelo filtrado (comp 8:1 + filtros, mezcla bajo seco) */
    struct ParallelBus
    {
        void prepare (double sr, int nblock, int nch);
        void reset();
        float process (juce::AudioBuffer<float>& dry, float amount);
        juce::AudioBuffer<float> par;
        juce::dsp::IIR::Filter<float> hpIn, lpIn, hpOut, lpOut;
        EnvFollower env;
        juce::LinearSmoothedValue<float> mix { 0.0f };
        double sampleRate = 0; int nch = 2;
    };

    /* 10: tape glue final (Studer A800 retuneado a VOZ, del modelo de
       SignoffNative): 30 IPS (drive bajo, saturación sutil), SIN head bump
       (el HPF adaptativo ya limpia 40-150 Hz), pre-énfasis +2 dB @ 3.5k →
       tanh normalizado → de-énfasis (tercer armónico suave, no "arena"),
       compresión de picos por envolvente common-mode (L+R)/2 hasta ~3.5 dB
       (el "laminador") y DC blocker. Mezcla dry/wet escalada por Shine.
       Cero latencia, cero allocs en audio. */
    struct TapeStage
    {
        void prepare (double sr, int block, int nch);
        void reset();
        float process (juce::AudioBuffer<float>& b, float amount);
        juce::dsp::IIR::Filter<float> pre[2], de[2], dc[2];
        EnvFollower env;
        double sampleRate = 0; int nch = 2;
    };

    DeEsser deesser;
    OptoComp optical;
    ResonanceSuppressor suppressor;
    HarmonicExciter exciter;
    EqStage eq;
    ParallelBus parallel;
    TapeStage tape;

    LearnProfile profile;
    juce::LinearSmoothedValue<float> amount_s { 0.0f };
    juce::LinearSmoothedValue<float> bodyAir_s { 0.0f };

    float compGR = 0.0f, parGR = 0.0f;
    double sampleRate = 0; int numCh = 2;

public:
    /* Bits de etapa para depuración/self-test (1=EQ,2=de-esser,4=óptico,
        8=supresor,16=exciter,32=paralelo,64=tape). */
    enum PatchedStage : unsigned int
    {
        kStageEq        = 1u << 0,
        kStageDeEsser   = 1u << 1,
        kStageOptical   = 1u << 2,
        kStageSuppressor= 1u << 3,
        kStageExciter   = 1u << 4,
        kStageParallel  = 1u << 5,
        kStageTape      = 1u << 6,
    };
};
