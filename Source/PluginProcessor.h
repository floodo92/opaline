#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/VoiceChain.h"
#include "DSP/LearnEngine.h"
#include "DSP/GainMatcher.h"
#include "InstallPing.h"

/* ============================================================
   OPALINE — polisher vocal de lujo (serie free).
   Un knob Shine escala toda la cadena de brillo "sedoso/caro";
   Measure calibra los centros a la voz concreta. Gain-matcher
   automático para A/B honesto.
   ============================================================ */
class OpalineAudioProcessor : public juce::AudioProcessor
{
public:
    OpalineAudioProcessor();
    ~OpalineAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Opaline"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override
    { return apvts.getParameter ("bypass"); }

    /* Measure (Learn) — se dispara desde la UI bajo mutex seguro. */
    void beginMeasure();
    void endMeasure();
    bool isMeasuring() const noexcept { return learn.isMeasuring(); }
    float getMeasureProgress() const noexcept { return learn.getProgress(); }
    const LearnProfile& getProfile() const noexcept { return chain.getLearnProfile(); }
    bool hasMeasured() const noexcept { return hasMeasuredFlag.load (std::memory_order_relaxed); }

    /* Posición recomendada del knob Shine (0..100) para el perfil medido. */
    float getRecommendedShine() const;

    /* Diagnóstico del último Learn (se lee desde la UI): 1=válido, 2=sin señal,
       3=sin sib/res (señal sin voz). Energías y nº de ventanas analizadas. */
    int getLastFinishReason() const noexcept { return learn.getLastFinishReason(); }
    double getLastBodyEnergy() const noexcept { return learn.getLastBodyEnergy(); }
    double getLastSibEnergy()  const noexcept { return learn.getLastSibEnergy(); }
    long long getLastWindows() const noexcept { return learn.getLastWindows(); }

    /* Atomics para la UI (meters, GR). */
    float getShineGR() const noexcept { return grShine.load(); }
    std::pair<float,float> getInLevels() const noexcept { return { inL.load(), inR.load() }; }
    std::pair<float,float> getOutLevels() const noexcept { return { outL.load(), outR.load() }; }

    /* Canales de la sesión real (ground-truth del último bloque): 0 = no hubo
       sesión aún (footer "--"), 1=MONO, 2=STEREO, >2 = N canales reales. Es la
       verdad del bloque, no lo que el host declara en un getTotalNum* que
       puede quedarse en el layout por defecto. */
    int getSessionChannels() const noexcept { return sessionChans.load (std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();

    VoiceChain chain;
    LearnEngine learn;
    GainMatcher gainMatcher;

    /* Escrito en el hilo de audio tras confirmar el Learn; leído por la UI. */
    std::atomic<bool> hasMeasuredFlag { false };
    std::atomic<bool> confirmRequest { false };

    std::atomic<int> sessionChans { 0 };
    std::atomic<float> grShine { 0.0f };
    std::atomic<float> inL { 0.0f }, inR { 0.0f }, outL { 0.0f }, outR { 0.0f };

    /* Delay feed-forward del bypass: mantiene la latencia reportada al DAW
       también con BYPASS activo (conmutación sin salto de timing, §10.14). */
    juce::AudioBuffer<float> bypassDelay;
    int bypassDelayPos = 0;
    int bypassLatency = 0;

    /* Estado de bypass del bloque anterior: en cada transición se limpia la
       cadena y se fijan los parámetros sin rampa (A/B idéntico, sin deriva). */
    bool wasBypassed = true;

    /* Auto-trim del Learn: sube la entrada al punto dulce y compensa con la
       inversa exacta a la salida (loudness neta idéntica). Rampa 300 ms para
       que ni siquiera el cambio de punto de operación se note. `trimTargetDb`
       es el valor persistente entre prepareToPlay. */
    juce::LinearSmoothedValue<float> trimIn { 1.0f };
    float trimTargetDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpalineAudioProcessor)
};
