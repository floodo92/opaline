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
    void setLevelProvider (LevelProvider p);

    void paint (juce::Graphics&) override;

    static constexpr int preferredHeight = 52; /* .led-meter-wrap */

private:
    void timerCallback() override;

    juce::String labels[2] { "CH L", "CH R" };
    LevelProvider provider;

    float smooth[2] { -60.0f, -60.0f };
    float peaks[2]  { -60.0f, -60.0f };
    int   peakTime[2] { 0, 0 };
};

} // namespace floodo::brand
