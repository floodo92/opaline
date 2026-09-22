// Opaline_stories.cpp
//
// Story: renders the full compact one-knob editor headlessly.
// Golden target: tests/snapshots/golden/opaline_mockup_400.png
// (mock 960x1112 downscaled to the 400x460 design canvas).
#include "../SnapshotApi.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <memory>
#include <utility>

struct OpalineEditorHolder : public juce::Component
{
    std::unique_ptr<OpalineAudioProcessor> proc;
    std::unique_ptr<OpalineAudioProcessorEditor> editor;

    OpalineEditorHolder()
    {
        setOpaque (true);
    }

    void resized() override
    {
        if (editor != nullptr)
            editor->setBounds (getLocalBounds());
    }
};

REGISTER_COMPONENT_SIZED (OpalineFull, []{
    auto h = std::make_unique<OpalineEditorHolder>();

    h->proc   = std::make_unique<OpalineAudioProcessor>();
    h->editor = std::unique_ptr<OpalineAudioProcessorEditor> (
        static_cast<OpalineAudioProcessorEditor*> (h->proc->createEditor()));
    h->addAndMakeVisible (h->editor.get());
    h->editor->setSize (floodo::brand::canvasW, floodo::brand::canvasH);

    // Valores de partida = los del mockup aprobado (opaline_design.html:
    // SHINE 0, BODY/AIR 0, OUTPUT 70) para que el diff sea comparable 1:1.
    if (auto* p = h->proc->apvts.getParameter ("shine"))
        p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
    if (auto* p = h->proc->apvts.getParameter ("bodyair"))
        p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
    if (auto* p = h->proc->apvts.getParameter ("output"))
        p->setValueNotifyingHost (p->convertTo0to1 (70.0f));

    // Sin audio los meters salen apagados y no se puede evaluar su diseño.
    // Empujamos un tono (≈ −14 dBFS, nivel de voz típico) por processBlock:
    // así los niveles son REALES, no valores decorativos.
    constexpr double sr = 48000.0;
    constexpr int    bs = 512;
    h->proc->prepareToPlay (sr, bs);

    {
        juce::AudioBuffer<float> buf (2, bs);
        juce::MidiBuffer midi;
        double phase = 0.0;
        const double inc = juce::MathConstants<double>::twoPi * 220.0 / sr;

        for (int blk = 0; blk < 80; ++blk)
        {
            for (int i = 0; i < bs; ++i)
            {
                const auto s = (float) (0.2 * std::sin (phase));
                phase += inc;
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
            h->proc->processBlock (buf, midi);
        }
    }

    // El smoothing de los meters converge dentro de paint(), y el snapshot
    // pinta una sola vez. Forzamos unos frames contra un Image descartable
    // para que el render muestre el estado asentado.
    {
        juce::Image warm (juce::Image::ARGB, 400, 460, true);
        for (int i = 0; i < 60; ++i)
        {
            juce::Graphics g (warm);
            h->paintEntireComponent (g, false);
        }
    }

    return h;
}, 400, 460);

// Frame completo (one-knob) a la vista: sesión recall con "measured" persistido
// (setStateInformation) revela el editor real; el gate de primera calibración
// NO se muestra. Valores de partida del mockup (SHINE 0, BODY/AIR 0, OUTPUT
// 70) + audio estéreo para footer "48.0 kHz · STEREO" y meters vivos.
REGISTER_COMPONENT_SIZED (OpalineFrameReady, []{
    auto h = std::make_unique<OpalineEditorHolder>();

    h->proc = std::make_unique<OpalineAudioProcessor>();
    juce::ValueTree st ("OPALINE");
    st.setProperty ("measured", true, nullptr);
    for (auto& pid : { "shine", "bodyair", "output", "bypass" })
    {
        auto* p = h->proc->apvts.getParameter (pid);
        st.setProperty (pid, p->getValue(), nullptr);
    }
    juce::MemoryBlock mb;
    juce::MemoryOutputStream mo (mb, false);
    st.writeToStream (mo);
    h->proc->setStateInformation (mb.getData(), (int) mb.getSize());

    h->editor = std::unique_ptr<OpalineAudioProcessorEditor> (
        static_cast<OpalineAudioProcessorEditor*> (h->proc->createEditor()));
    h->addAndMakeVisible (h->editor.get());
    h->editor->setSize (floodo::brand::canvasW, floodo::brand::canvasH);

    if (auto* p = h->proc->apvts.getParameter ("shine"))
        p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
    if (auto* p = h->proc->apvts.getParameter ("bodyair"))
        p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
    if (auto* p = h->proc->apvts.getParameter ("output"))
        p->setValueNotifyingHost (p->convertTo0to1 (70.0f));

    constexpr double sr = 48000.0;
    constexpr int    bs = 512;
    h->proc->prepareToPlay (sr, bs);

    {
        juce::AudioBuffer<float> buf (2, bs);
        juce::MidiBuffer midi;
        double phase = 0.0;
        const double inc = juce::MathConstants<double>::twoPi * 220.0 / sr;
        for (int blk = 0; blk < 80; ++blk)
        {
            for (int i = 0; i < bs; ++i)
            {
                const auto s = (float) (0.2 * std::sin (phase));
                phase += inc;
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
            h->proc->processBlock (buf, midi);
        }
    }

    {
        juce::Image warm (juce::Image::ARGB, 400, 460, true);
        for (int i = 0; i < 60; ++i)
        {
            juce::Graphics g (warm);
            h->paintEntireComponent (g, false);
        }
    }

    return h;
}, 400, 460);