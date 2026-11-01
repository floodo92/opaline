#pragma once
#include "Brand.h"
#include <functional>

namespace floodo::brand
{
/* LED meter segmentado −40..+3 dB, look Signoff (BRAND.md §"meters"):
   gris neutro = todo bien · ámbar −6..0 · rojo ≥ 0 · peak-hold.
   Canvas 1:1 de LedMeter.jsx. Los niveles llegan vía provider (atomics del
   processor); el componente corre su propio timer a ~60 fps. */
class LedMeter : public juce::Component,
                 private juce::Timer
{
public:
    using LevelProvider = std::function<std::pair<float, float>()>;

    LedMeter();

    void setChannelLabels (const juce::String& ch0, const juce::String& ch1);
    void setTitle (const juce::String& t) { title = t.toUpperCase(); repaint(); }
    void setLevelProvider (LevelProvider p);

    /* Modo panel (Opaline): título + 2 filas etiqueta/barra + graticule +
       readout de dB por canal. Geometría fija de 72×104. */
    void setCompact (bool c) { compact = c; repaint(); }

    void paint (juce::Graphics&) override;

    static constexpr int preferredHeight = 52; /* .led-meter-wrap */

private:
    void timerCallback() override;

    juce::String labels[2] { "L", "R" };
    juce::String title { "IN" };
    LevelProvider provider;

    float smooth[2]  { -60.0f, -60.0f };
    float readout[2] { -60.0f, -60.0f }; /* seguidor lento del número (estable) */
    float peaks[2]   { -60.0f, -60.0f };
    int   peakTime[2] { 0, 0 };
    bool  compact = false;
};

} // namespace floodo::brand
