#pragma once
#include "PluginProcessor.h"
#include "UI/Knob.h"
#include "UI/LedMeter.h"
#include "UI/Module.h"

/* Editor de OPALINE — rediseño compacto "one-knob" (serie free).
   Canvas cuadrado 410×440:
     ┌──────────────────────────────┐
     │  OPALINE          MEAS  BYP  │   52  header
     │  [ IN meter ]  [ OUT meter ] │   58  meters
     │         ┌────────┐           │
     │         │ SHINE  │           │   hero
     │         └────────┘           │
     │   BODY/AIR        OUTPUT     │   knobs
     │   voice calibrated           │   status
     └──────────────────────────────┘
   Todo vive en coords de diseño; el editor escala la Frame como unidad. */

/* Look & feel de los botones tipo "pill" (MEASURE / BYPASS). Pequeño,
   específico del editor: no toca el sistema de marca global. */
class OpalineButtonsLAF : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                               const juce::Colour&, bool hover, bool) override
    {
        const bool on  = b.getToggleState();
        const bool acc = b.getComponentID() == "measure";
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);

        g.setColour (on ? (acc ? floodo::brand::accentAlpha (0.26f)
                               : juce::Colour (floodo::brand::cSafe).withAlpha (0.20f))
                        : juce::Colour (floodo::brand::raised));
        g.fillRoundedRectangle (r, floodo::brand::rSm);
        if (hover)
        {
            g.setColour (floodo::brand::accentAlpha (0.10f));
            g.fillRoundedRectangle (r, floodo::brand::rSm);
        }
        g.setColour (on ? (acc ? floodo::brand::accent()
                               : juce::Colour (floodo::brand::cSafe))
                        : floodo::brand::lineStrong());
        g.drawRoundedRectangle (r, floodo::brand::rSm, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b,
                         bool hover, bool) override
    {
        const bool on  = b.getToggleState();
        const bool acc = b.getComponentID() == "measure";
        g.setFont (floodo::brand::font (floodo::brand::tDisplaySM()));
        g.setColour (on ? (acc ? floodo::brand::accent()
                               : juce::Colour (floodo::brand::cSafe))
                        : (hover ? juce::Colours::white.withAlpha (0.8f)
                                 : juce::Colour (floodo::brand::textMid)));
        g.drawText (b.getButtonText(), b.getLocalBounds(),
                    juce::Justification::centred, false);
    }
};

class OpalineAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer
{
public:
    explicit OpalineAudioProcessorEditor (OpalineAudioProcessor&);
    ~OpalineAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Frame : juce::Component
    {
        explicit Frame (OpalineAudioProcessorEditor& owner);
        void paint (juce::Graphics&) override;
        void resized() override;

        OpalineAudioProcessorEditor& ed;

        floodo::brand::Knob shineKnob  { 140 };
        floodo::brand::Knob bodyAirKnob{ 84 };
        floodo::brand::Knob outputKnob { 84 };
        floodo::brand::LedMeter inMeter, outMeter;

        juce::TextButton measureBtn;
        juce::TextButton bypassBtn;
        juce::Label measureStatus;

        std::unique_ptr<floodo::brand::KnobAttachment> shineAtt, bodyAirAtt, outputAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;
        std::unique_ptr<OpalineButtonsLAF> laf { new OpalineButtonsLAF() };
    };

    void measurePressed();
    void timerCallback() override;
    void positionShineForProfile();
    void updateMeasureStatus();

    OpalineAudioProcessor& proc;
    Frame frame;
    float currentScale = 1.0f;
    bool measuring = false;
    bool pendingPosition = false;
    int debounceTicks = 0;   // UI espera al confirm del hilo de audio antes de juzgar

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpalineAudioProcessorEditor)
};