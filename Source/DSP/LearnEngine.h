#pragma once
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

/* ============================================================
   LEARNPROFILE — el resultado del Learn: centros y sensibilidades
   calibrados a la voz concreta. La cadena (VoiceChain) los consume.
   ============================================================ */
struct LearnProfile
{
    float deEssFreq      = 6800.0f;   // centro de sibilancia (Hz) 5-10k
    float resonanceFreq  = 3400.0f;   // centro de la dureza (Hz) 2-5k
    float lowMidFreq     = 320.0f;    // lodos/proximidad (Hz)
    float airFreq        = 12000.0f;  // shelf de aire (Hz)
    float presenceFreq   = 2500.0f;   // presencia (Hz)

    float sibilanceLevel = 0.5f;      // 0..1 cuánta sibilancia había
    float resonanceLevel = 0.5f;      // 0..1 cuánta dureza había
    float lowMidLevel    = 0.5f;      // 0..1 cuánto lodo había
    float brightness     = 0.5f;      // 0..1 brillo/presencia disponible
    float dynamics       = 0.5f;      // 0..1 rango/profundidad comp

    // Pendiente espectral objetivo (Learn): empuje de AGUDOS (presencia 2-4k)
    // necesario para que queden ~5 dB por debajo del cuerpo (500-1000 Hz),
    // la regla del triángulo de Paco Rincón. Negativo = hay que bajar presencia;
    // positivo = hay que subirla. Es la "B" de referencia del triángulo.
    float airTargetDb    = 0.0f;

    // Regla de sibilancias (referencia Paco Rincón): las "S" nunca deben superar
    // el nivel del cuerpo (500-1k). Se mide como TRANSIENTE (percentil del pico
    // de la banda 5-10k vs la media del cuerpo), no promedio. >0 hay que domearlas.
    float sibExcessDb    = 0.0f;

    float medianPitchHz  = 220.0f;    // mediana de la fundamental
    bool  valid          = false;     // true si se calibró al menos una vez
};

/* ============================================================
   LEARNENGINE — análisis de la take para calibrar LEARNPROFILE.
   STFT por streaming con búfer interno: consume lo que mida el
   bloque del host y acumula lo que falta, por lo que funciona con
   bloques de 128..2048+ muestras. Acumula energía por bandas,
   centro = bin de máxima magnitud, sibilancia por percentil de
   exceso transitorio y pitch por autocorrelación. Corre en el hilo
   de audio con todo pre-dimensionado (sin allocs).
   ============================================================ */
class LearnEngine
{
public:
    LearnEngine() = default;

    void prepare (double sampleRate, int maxBlockSize);

    /* Mientras active==true, consume audio para análisis. */
    void process (const juce::AudioBuffer<float>& buffer, bool active);

    /* Termina la captura y produce el perfil calibrado. */
    LearnProfile finish();

    bool isMeasuring() const noexcept { return measuring.load (std::memory_order_relaxed); }
    void setMeasuring (bool m);

    /* Progreso 0..1 de la captura (para la UI). */
    float getProgress() const noexcept;

    /* Motivo de cierre del último finish(): 1=ok, 2=sin señal, 3=sin sib/res,
       4=tope de silencio (gate force-cerró). Para diagnóstico en la UI. */
    int getLastFinishReason() const noexcept { return lastReason.load (std::memory_order_relaxed); }

    /* Valores de energía del último finish (diagnóstico) antes de resetear. */
    double getLastBodyEnergy()  const noexcept { return lastBodyEnergy.load (std::memory_order_relaxed); }
    double getLastSibEnergy()   const noexcept { return lastSibEnergy.load (std::memory_order_relaxed); }
    long long getLastWindows()   const noexcept { return lastWindows.load (std::memory_order_relaxed); }

    /* Ventanas FFT acumuladas (para diagnóstico/regresión, no bloqueante). */
    long long getAnalyzedWindows() const noexcept { return analyzedWindows.load (std::memory_order_relaxed); }

private:
    static constexpr int  kFftOrder = 11;   // 2048
    static constexpr int  kFftSize  = 1 << kFftOrder;
    static constexpr int  kHop      = 512;

    /* Gate de "voz REAL": el progreso de captura solo avanza cuando una ventana
       trae cuerpo (500-1k) por encima de este piso (piso en ENERGÍA, mag²).
       Así, si se pulsa Measure en silencio, el Learn NO cierra a los 4 s con
       un perfil vacío: espera a que entre voz (equivale a que el primer y el
       segundo Learn midan igual, y a que las referencias de la sesión queden
       en el mismo punto). Tope de seguridad: si pasan muchas ventanas mudas
       consecutivas, se cierra igual (inválido) y la UI no mueve el Shine. */
    static constexpr float kBodyFloor = 1e-5f;                 // ≈ -50 dBFS mag²
    static constexpr int   kMaxQuietWindows = 2400;             // tope de voz: ~30 s a 48k/hop512

    int quietWindows = 0;   // ventanas seguidas bajo el piso de cuerpo (voz)

    /* Pitch por autocorrelación a 44.1k — devuelve Hz o 0 si no-pitched. */
    static float autocorrelate (const float* x, int n, double sr);

    struct Buckets
    {
        double sib = 0.0;   // energía 5-10k
        double res = 0.0;   // energía 2-5k
        double lowMid = 0.0;// energía 200-500
        double pres = 0.0;  // energía 2-4k (agudos de la regla del triángulo)
        double body = 0.0;  // energía 500-1000 (referencia de "cuerpo")
        double total = 0.0;
        double pitchSum = 0.0;
        double pitchCount = 0.0;
        double peakSib = 0.0, peakRes = 0.0;   // picos para encontrar centro
        double sibFreqAcc = 0.0, resFreqAcc = 0.0; // acumulan centro
        float excTop[32] = {};   // exceso transitorio de sib por ventana (dB)
        int   excCount = 0;
    };
    Buckets b;

    double sampleRate = 48000.0;
    int    maxBlock = 512;

    /* Búfer de streaming: pre-dimensionado en prepare() (NUNCA allocs en audio). */
    std::vector<float> monoBuf;
    int    monoCount = 0;
    juce::dsp::FFT fft { kFftOrder };
    std::vector<float> window;
    std::vector<float> fftData;
    std::vector<float> mono;

    /* Estado compartido con la UI: accesos desde audio y desde la UI. */
    std::atomic<bool> measuring { false };
    std::atomic<int>  accumSamples { 0 };
    std::atomic<int>  targetSamples { 0 };
    std::atomic<long long> analyzedWindows { 0 };
    /* La UI pide el reset de los acumuladores; la ejecución del reset ocurre en
       el hilo de audio (único dueño de `b`) para no pisar el análisis. */
    std::atomic<bool> pendingReset { false };

    /* Diagnóstico del último cierre (lectura desde la UI, escritura en audio): */
    std::atomic<int>  lastReason { 0 };
    std::atomic<double> lastBodyEnergy { 0.0 };
    std::atomic<double> lastSibEnergy  { 0.0 };
    std::atomic<long long> lastWindows  { 0 };
};