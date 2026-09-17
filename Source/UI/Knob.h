#pragma once
#include "Brand.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

namespace floodo::brand
{
/* ============================================================
   KNOB — la firma de la marca. Port 1:1 del canvas 2D de
   Knob.jsx (Cascade). NO aplanar: cada capa es parte de la
   identidad (ver BRAND.md §3):
     1 sombra proyectada        6 puntero lanza blanco
     2 escala grabada + retícula 7 sello central accent
     3 arco de valor + índice   8 sheen hover/drag
     4 cara abombada grafito    9 bevels de aro hairline
     5 faceta maquinada rotatoria
   Interacción: drag vertical (Shift = fino), wheel, dbl-click reset,
   detents con `steps`, marcador de default y zero bipolar.
   ============================================================ */
class Knob : public juce::Component
{
public:
    explicit Knob (float knobSize = 96.0f);

    void setValue (double v, bool notify = true);
    double getValue() const noexcept            { return value; }

    void setRange (double newMin, double newMax);
    void setDefaultValue (double v);            /* < min = sin marker */
    void setBipolar (bool b)                    { bipolar = b; repaint(); }
    void setSteps (int n)                       { steps = n; repaint(); }
    void setScaleLabels (const juce::StringArray& l) { scaleLabels = l; repaint(); }
    void setReadoutReveal (bool reveal)         { readoutReveal = reveal; repaint(); }

    void setShowLabel (bool show)               { showLabel = show; resized(); }
    void setLabelText (const juce::String& t)   { labelText = t.toUpperCase(); resized(); }
    void setFormatValue (std::function<juce::String (double)> f) { formatValue = std::move(f); repaint(); }

    std::function<void (double)> onChange;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int readoutH = 14;   /* .knob-val 11.5px line-height 1 */
    static constexpr int labelH   = 12;   /* .knob-lbl 10px line-height 1 */
    static constexpr int rowGap   = 5;    /* .knob-wrap gap */

    /* Altura total (canvas + readout + label) para que el padre posicione */
    float totalHeight() const noexcept
    { return knobDiameter() + rowGap + readoutH + (showLabel ? labelH : 0); }

    /* Diámetro del canvas completo (knob + pad), público para layout */
    float diameter() const noexcept { return knobDiameter(); }

private:
    float knobDiameter() const noexcept { return size + pad * 2.0f; }

    float size;       /* diámetro lógico del knob (CSS px) */
    float pad;        /* padSize — margen para sombra/arco */

    double value = 50, min = 0, max = 100, defaultValue = -1e300;
    int    steps = 0;
    bool   bipolar = false;
    bool   hovered = false, dragging = false;
    bool   showLabel = false, readoutReveal = false;
    juce::String labelText;
    juce::StringArray scaleLabels;
    std::function<juce::String (double)> formatValue;

    double dragStartVal = 0;
    float  dragStartY = 0;

    bool isActive() const noexcept { return hovered || dragging; }
    double clampNorm (double n) const;
    double snapIfNeeded (double raw) const;
    juce::String displayText() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

/* Sincroniza un Knob (lineal min..max) con un parámetro float del APVTS. */
class KnobAttachment
{
public:
    KnobAttachment (juce::AudioProcessorValueTreeState& state,
                    const juce::String& paramID,
                    Knob& knobToUpdate);
    ~KnobAttachment();

private:
    void updateKnob();
    Knob& knob;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};
} // namespace floodo::brand
