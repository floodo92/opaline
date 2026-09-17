#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>

OpalineAudioProcessor::OpalineAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "OPALINE", makeLayout())
{
}

OpalineAudioProcessor::~OpalineAudioProcessor() = default;

bool OpalineAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Permisivo: acepta mono y estéreo en entrada y diseño de buses por defecto.
    const auto& mainIn = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const bool inOK = mainIn.isDisabled()
        || mainIn.size() == 1 || mainIn.size() == 2;
    const bool outOK = mainOut.isDisabled()
        || mainOut.size() == 1 || mainOut.size() == 2;
    return inOK && outOK;
}

juce::AudioProcessorValueTreeState::ParameterLayout
OpalineAudioProcessor::makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "shine", "Shine",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "bodyair", "Body / Air",
        juce::NormalisableRange<float> { -100.0f, 100.0f, 0.1f }, 0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "output", "Output",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 70.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> ("bypass", "Bypass", false));

    return layout;
}

void OpalineAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int nch = juce::jmax (1, getTotalNumInputChannels());
    chain.prepare (sampleRate, samplesPerBlock, nch);
    chain.setAmount (apvts.getRawParameterValue ("shine")->load() / 100.0f, true);
    chain.setBodyAir (apvts.getRawParameterValue ("bodyair")->load() / 100.0f);
    learn.prepare (sampleRate, samplesPerBlock);
    gainMatcher.prepare (sampleRate, samplesPerBlock);

    // Latencia real del oversampling del exciter -> el DAW la compensa.
    setLatencySamples (chain.getLatencySamples());
}

void OpalineAudioProcessor::releaseResources()
{
}

void OpalineAudioProcessor::beginMeasure()
{
    learn.setMeasuring (true);
    learn.prepare (getSampleRate(), getBlockSize());
}

void OpalineAudioProcessor::endMeasure()
{
    if (learn.isMeasuring())
    {
        learn.setMeasuring (false);
        confirmRequest = true;    // finish en el hilo de audio (sin races)
    }
}

float OpalineAudioProcessor::getRecommendedShine() const
{
    const auto& p = chain.getLearnProfile();
    if (!p.valid)
        return 0.0f;

    // "Lugar justo" para esta voz: parte de un valor MEDIO y sube solo según lo
    // que la take pide. Pesos deliberadamente comedidos: la presencia (aire) no
    // manda por sí sola (una voz con cuerpo sano y algo de brillo no necesita
    // 70%+; una take típica cae ~40-55%, y solo una voz apagada de verdad sube).
    float rec = 0.30f
              + 0.016f * juce::jlimit (0.0f, 15.0f, p.airTargetDb)
              + 0.004f * juce::jlimit (0.0f, 24.0f, p.sibExcessDb)
              + 0.050f * juce::jlimit (0.0f,  1.0f, p.lowMidLevel);
    rec = juce::jlimit (0.22f, 0.62f, rec);
    return rec * 100.0f;
}

void OpalineAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const bool bypassed = apvts.getRawParameterValue ("bypass")->load() > 0.5f;
    const float amount  = apvts.getRawParameterValue ("shine")->load() / 100.0f;
    const float bodyAir = apvts.getRawParameterValue ("bodyair")->load() / 100.0f;
    const float outDb   = juce::jmap (apvts.getRawParameterValue ("output")->load(), 0.0f, 100.0f, -24.0f, 12.0f);

    // Confirmación del Learn (se resuelve aquí, en el hilo de audio).
    if (confirmRequest && !learn.isMeasuring())
    {
        auto prof = learn.finish();
        if (prof.valid)
        {
            chain.setLearnProfile (prof);
            hasMeasuredFlag = true;
        }
        confirmRequest = false;
    }

    // Alimenta el Learn mientras mide; al completar la captura (~4 s) se cierra
    // solo, sin que el usuario tenga que volver a pulsar Measure.
    if (learn.isMeasuring())
    {
        learn.process (buffer, true);
        if (learn.getProgress() >= 1.0f)
        {
            learn.setMeasuring (false);
            confirmRequest = true;
        }
    }

    // Transición de bypass: estado DSP limpio y parámetros fijados sin rampa.
    // Así cada "on/off" suena idéntico y no se acumula ningún cambio tonal.
    if (bypassed != wasBypassed)
    {
        chain.reset();
        chain.setAmount  (amount,  true);
        chain.setBodyAir (bodyAir, true);
        wasBypassed = bypassed;
    }

    const int n = buffer.getNumSamples();

    // Medición de entrada SIEMPRE (independiente del bypass).
    float rmsIn = 0.0f;
    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
        for (int i = 0; i < n; ++i)
            rmsIn += buffer.getSample (ch, i) * buffer.getSample (ch, i);
    rmsIn = std::sqrt (rmsIn / (juce::jmax (1, n * buffer.getNumChannels())));

    // Meters de entrada (pre-proceso).
    float peakIn[2] = { 0.0f, 0.0f };
    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
        for (int i = 0; i < n; ++i)
            peakIn[ch] = juce::jmax (peakIn[ch], std::abs (buffer.getSample (ch, i)));

    if (!bypassed)
    {
        chain.setAmount (amount);
        chain.setBodyAir (bodyAir);

        chain.process (buffer);

        // Output trim (dB). Después del gain-match para que el trim sea el
        // control final de volumen del usuario.
        buffer.applyGain (juce::Decibels::decibelsToGain (outDb));

        // RMS out y gain-match.
        float rmsOut = 0.0f;
        for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
            for (int i = 0; i < n; ++i)
                rmsOut += buffer.getSample (ch, i) * buffer.getSample (ch, i);
        rmsOut = std::sqrt (rmsOut / (juce::jmax (1, n * buffer.getNumChannels())));

        const float g = gainMatcher.process (rmsIn, rmsOut);
        buffer.applyGain (g);

        grShine.store (chain.getCompGR());
    }

    // Meters de salida.
    float peakOut[2] = { 0.0f, 0.0f };
    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
        for (int i = 0; i < n; ++i)
            peakOut[ch] = juce::jmax (peakOut[ch], std::abs (buffer.getSample (ch, i)));

    inL.store (peakIn[0]); inR.store (peakIn[1]);
    outL.store (peakOut[0]); outR.store (peakOut[1]);
}

juce::AudioProcessorEditor* OpalineAudioProcessor::createEditor()
{
    return new OpalineAudioProcessorEditor (*this);
}

void OpalineAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    if (hasMeasuredFlag)
    {
        const auto& p = chain.getLearnProfile();
        state.setProperty ("measured", true, nullptr);
        state.setProperty ("deEssFreq",     p.deEssFreq, nullptr);
        state.setProperty ("resFreq",       p.resonanceFreq, nullptr);
        state.setProperty ("lowMidFreq",    p.lowMidFreq, nullptr);
        state.setProperty ("sibLevel",      p.sibilanceLevel, nullptr);
        state.setProperty ("resLevel",      p.resonanceLevel, nullptr);
        state.setProperty ("brightness",    p.brightness, nullptr);
    }
    juce::MemoryOutputStream mo (dest, false);
    state.writeToStream (mo);
}

void OpalineAudioProcessor::setStateInformation (const void* data, int size)
{
    auto tree = juce::ValueTree::readFromData (data, (size_t) size);
    if (tree.isValid())
    {
        apvts.replaceState (tree);
        if (tree.getProperty ("measured", false))
        {
            LearnProfile p = chain.getLearnProfile();
            p.deEssFreq      = (float) tree.getProperty ("deEssFreq", p.deEssFreq);
            p.resonanceFreq  = (float) tree.getProperty ("resFreq", p.resonanceFreq);
            p.lowMidFreq     = (float) tree.getProperty ("lowMidFreq", p.lowMidFreq);
            p.sibilanceLevel = (float) tree.getProperty ("sibLevel", p.sibilanceLevel);
            p.resonanceLevel = (float) tree.getProperty ("resLevel", p.resonanceLevel);
            p.brightness     = (float) tree.getProperty ("brightness", p.brightness);
            p.valid = true;
            chain.setLearnProfile (p);
            hasMeasuredFlag = true;
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OpalineAudioProcessor();
}
