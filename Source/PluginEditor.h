#pragma once
#include "PluginProcessor.h"
#include "UI/Knob.h"
#include "UI/LedMeter.h"
#include "UI/Module.h"

/* Editor de OPALINE — rediseño "instrumento de precisión" (serie free).
   Canvas 400×460 (coords de diseño; el editor escala la Frame como unidad):
     ┌──────────────────────────────┐
     │ OPALINE        [BYPASS][MEAS] │  header #0e1013 sobre canvas #060708
     │ VOCAL POLISH                  │
     ├──────────────────────────────┤
     │  IN ▬▬          OUT ▬▬       │  meters 72×104 (título + L/R + dB)
     │         ╭──────╮             │
     │         │SHINE │             │  hero: pocillo + disco de acero
     │         ╰──────╯             │         + índices + arco de valor
     │  BODY/AIR        OUTPUT       │
     │ 48.0 kHz · STEREO   ● READY  │  footer inset
     └──────────────────────────────┘
   Geometría exacta y por qué: ver opaline_design.html (fuente de verdad). */

/* Look & feel de los botones del header (MEASURE / BYPASS).
   Placa plana grafito + hairline, sin relieve. El estado activo sube un
   escalón la superficie y enciende el sheen ópalo de 24x2 en el borde
   superior: es la ÚNICA concesión de marca del layout. */
class OpalineButtonsLAF : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                               const juce::Colour&, bool hover, bool isDown) override
    {
        const bool on  = b.getToggleState();
        const bool bps = (b.getComponentID() == "bypass");
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (on ? juce::Colour (floodo::brand::panelMid)
                        : juce::Colour (floodo::brand::panel));
        g.fillRoundedRectangle (r, floodo::brand::rSm);

        if (hover && ! on)
        {
            g.setColour (juce::Colours::white.withAlpha (0.04f));
            g.fillRoundedRectangle (r, floodo::brand::rSm);
        }

        /* Pressed: la placa se hunde — overlay oscuro sutil */
        if (isDown)
        {
            g.setColour (juce::Colours::black.withAlpha (0.10f));
            g.fillRoundedRectangle (r, floodo::brand::rSm);
        }

        /* Sheen superior — 24x2 px centrado en el borde superior.
           MEASURE: gradiente ópalo (concesión de marca).
           BYPASS:  verde cSafe (estado "seguro/enpasado" del bypass,
           intención documentada en OPALINE_CONTEXT.md). */
        if (on)
        {
            juce::Rectangle<float> bar (r.getCentreX() - 12.0f, r.getY(), 24.0f, 2.0f);
            if (bps)
            {
                g.setColour (juce::Colour (floodo::brand::cSafe).withAlpha (0.85f));
                g.fillRoundedRectangle (bar, 1.0f);
            }
            else
            {
                juce::ColourGradient sg (floodo::brand::opalCold(), bar.getX(), bar.getY(),
                                         floodo::brand::opalWarm(), bar.getRight(), bar.getY(), false);
                sg.addColour (0.55, floodo::brand::accent());
                g.setGradientFill (sg);
                g.fillRoundedRectangle (bar, 1.0f);
            }
        }

        g.setColour (on ? floodo::brand::lineStrong() : floodo::brand::line());
        g.drawRoundedRectangle (r, floodo::brand::rSm, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b,
                          bool, bool) override
    {
        /* Inter sólo tiene la instancia 400 disponible (fuente variable):
            el 500 del diseño cae a 400. Escala con la placa: los pills del
            header (28px) quedan en 9; el MEASURE del gate (46px) lee grande. */
        const float fs = juce::jmax (9.0f, b.getHeight() * 0.30f);
        g.setFont (floodo::brand::font ({ floodo::brand::FontId::Inter, fs,
                                          0.08f, false, true, 500 }));
        g.setColour (b.getToggleState() ? juce::Colour (floodo::brand::textHi)
                                        : juce::Colour (floodo::brand::textMid));
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

        /* size + pad(8)*2 = canvas: hero 156, chicos 72 */
        floodo::brand::Knob shineKnob  { 140 };
        floodo::brand::Knob bodyAirKnob{ 56 };
        floodo::brand::Knob outputKnob { 56 };
        floodo::brand::LedMeter inMeter, outMeter;

        juce::TextButton measureBtn;
        juce::TextButton bypassBtn;

        /* Footer: izquierda = dato real de sesión, derecha = estado del MEASURE
           (pintado a mano: dot de color + texto). */
        juce::Label  measureStatus;
        juce::String statusRight  { "READY" };
        juce::Colour statusColour { juce::Colour (floodo::brand::textLo) };

        std::unique_ptr<floodo::brand::KnobAttachment> shineAtt, bodyAirAtt, outputAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;
        std::unique_ptr<OpalineButtonsLAF> laf { new OpalineButtonsLAF() };
    };

    /* Gate de primera calibración (mismo patrón del Pro, portado a la free):
       instancia virgen → SOLO la pantalla de MEASURE. El audio NUNCA se
       bloquea (la UI manda: a Shine 0 la cadena es transparente). Al
       confirmar el perfil se revela el one-knob completo con el SHINE ya
       colocado. Escape: tras un intento fallido aparece CONTINUE WITHOUT
       CALIBRATION. Un recall de sesión medido NUNCA muestra el gate. */
    struct GateOverlay : juce::Component
    {
        explicit GateOverlay (OpalineAudioProcessorEditor& owner);
        void paint (juce::Graphics&) override;
        void resized() override;

        OpalineAudioProcessorEditor& ed;
        juce::TextButton measureGateBtn, skipGateBtn;
        std::unique_ptr<OpalineButtonsLAF> laf { new OpalineButtonsLAF() };
    };

    void measurePressed();
    void timerCallback() override;
    void positionShineForProfile();
    void updateFooter();
    void updateGateVisibility();

    OpalineAudioProcessor& proc;
    Frame frame;
    GateOverlay gate;
    bool gateDismissed = false;   // "CONTINUE WITHOUT CALIBRATION" (por instancia)
    float currentScale = 1.0f;
    bool measuring = false;
    bool pendingPosition = false;
    int debounceTicks = 0;   // UI espera al confirm del hilo de audio antes de juzgar

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpalineAudioProcessorEditor)
};