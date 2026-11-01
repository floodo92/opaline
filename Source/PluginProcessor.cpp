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
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float v, int)
            {
                return juce::String ((int) std::round (v));
            })));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "bodyair", "Body / Air",
        juce::NormalisableRange<float> { -100.0f, 100.0f, 0.1f }, 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int)
            {
                const int iv = (int) std::round (v);
                return (iv > 0 ? "+" : juce::String()) + juce::String (iv);
            })));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "output", "Output",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 70.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int)
            {
                return juce::String ((int) std::round (v));
            })));

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

    // Auto-trim: el objetivo (dB) sobrevive a los re-prepare del host; la
    // rampa de 300 ms se re-ancla a la tasa actual desde el valor ya aplicado.
    trimIn.reset (sampleRate, 0.30);
    trimIn.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (trimTargetDb));

    // Latencia real del oversampling del exciter -> el DAW la compensa.
    bypassLatency = chain.getLatencySamples();
    setLatencySamples (bypassLatency);

    // Delay del bypass: priming a ceros SIEMPRE en cada prepareToPlay
    // (si se llama varias veces, el retardo no se acumula).
    if (bypassLatency > 0)
    {
        bypassDelay.setSize (nch, bypassLatency, false, true, true);
        bypassDelay.clear();
    }
    bypassDelayPos = 0;

    opalineSendInstallPingOnce();
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

    /* Canal real del bloque (ground-truth para el footer: MONO/STEREO según lo
       que entrega el host, no según el layout por defecto del plugin). */
    sessionChans.store (buffer.getNumChannels(), std::memory_order_relaxed);

    // Saneado de entrada: un NaN/Inf proveniente de otro plugin no debe envenenar
    // los filtros ni los medidores (la cadena ya no se recuperaria sola).
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (d[i]))
                d[i] = 0.0f;
    }

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

            // Auto-trim al punto dulce: rampa de 300 ms hacia el objetivo;
            // entrada y salida se compensan inversas (abajo) -> la loudness
            // no cambia, solo el punto de operación de la cadena.
            trimTargetDb = prof.autoTrimDb;
            trimIn.setTargetValue (juce::Decibels::decibelsToGain (trimTargetDb));
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

    // Delay feed-forward del bypass (§10.14): SIEMPRE se alimenta con la
    // entrada (contenido fresco, conmutación wet/bypass continua); solo se
    // LEE el retardo cuando está bypassed, para mantener la latencia
    // reportada también en bypass.
    if (bypassLatency > 0)
    {
        const int chD = juce::jmin (bypassDelay.getNumChannels(), buffer.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < chD; ++c)
            {
                auto* d  = buffer.getWritePointer (c);
                auto* dl = bypassDelay.getWritePointer (c);
                const float delayed = dl[bypassDelayPos];
                dl[bypassDelayPos] = d[i];
                if (bypassed) d[i] = delayed;
            }
            bypassDelayPos = (bypassDelayPos + 1 == bypassLatency) ? 0 : bypassDelayPos + 1;
        }
    }

    if (!bypassed)
    {
        chain.setAmount (amount);
        chain.setBodyAir (bodyAir);

        // Auto-trim: ganancia por bloque (rampa 300 ms) a la ENTRADA del wet
        // path y su inversa EXACTA a la salida del chain. Loudness neta = 1:
        // el usuario no percibe cambio de volumen; solo la cadena empieza a
        // trabajar en su punto dulce (comp óptico y tanh del exciter).
        const float gTrim = trimIn.getNextValue();
        trimIn.skip (juce::jmax (0, n - 1));
        buffer.applyGain (gTrim);

        chain.process (buffer);

        buffer.applyGain (1.0f / gTrim);

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

    // Pista mono: el buffer trae 1 canal (o el bus principal es mono), así que
    // el meter R quedaría muerto. Espejamos L→R para que ambos midan igual.
    const bool mono = buffer.getNumChannels() < 2
                   || getMainBusNumInputChannels() < 2;
    inL.store  (peakIn[0]);
    inR.store  (mono ? peakIn[0] : peakIn[1]);
    outL.store (peakOut[0]);
    outR.store (mono ? peakOut[0] : peakOut[1]);
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
        state.setProperty ("airTargetDb",   p.airTargetDb, nullptr);
        state.setProperty ("sibExcessDb",   p.sibExcessDb, nullptr);
        state.setProperty ("medianPitchHz", p.medianPitchHz, nullptr);
        state.setProperty ("autoTrimDb",   p.autoTrimDb, nullptr);
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
            p.airTargetDb    = (float) tree.getProperty ("airTargetDb", p.airTargetDb);
            p.sibExcessDb    = (float) tree.getProperty ("sibExcessDb", p.sibExcessDb);
            p.medianPitchHz  = (float) tree.getProperty ("medianPitchHz", p.medianPitchHz);
            p.autoTrimDb     = (float) tree.getProperty ("autoTrimDb", p.autoTrimDb);
            p.valid = true;
            chain.setLearnProfile (p);
            hasMeasuredFlag = true;

            // Reanuda el auto-trim persistido (rampa suave desde el actual).
            trimTargetDb = p.autoTrimDb;
            trimIn.setTargetValue (juce::Decibels::decibelsToGain (trimTargetDb));
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OpalineAudioProcessor();
}
