#include "LedMeter.h"
#include "BrandFonts.h"

namespace floodo::brand
{
static constexpr float DB_MIN = -40.0f;
static constexpr float DB_MAX = 3.0f;

static float levelToDb (float level)
{
    if (level <= 0.0001f) return -60.0f;
    return juce::jlimit (-60.0f, DB_MAX, 20.0f * std::log10 (level));
}

static float dbToX (float db, float barX0, float barW)
{
    return barX0 + ((db - DB_MIN) / (DB_MAX - DB_MIN)) * barW;
}

LedMeter::LedMeter()
{
    setOpaque (true);
    startTimerHz (60);
}

void LedMeter::setChannelLabels (const juce::String& ch0, const juce::String& ch1)
{
    labels[0] = ch0;
    labels[1] = ch1;
}

void LedMeter::setLevelProvider (LevelProvider p) { provider = std::move (p); }

void LedMeter::timerCallback()
{
    if (isShowing()) repaint();
}

void LedMeter::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float W = bounds.getWidth();
    const float H = bounds.getHeight();

    /* Wrap: inset + borde hairline (el inner-shadow CSS se aproxima con
       un gradiente superior sutil). */
    g.setColour (Colour (inset));
    g.fillRoundedRectangle (bounds, rXs);

    /* Face — flat inset del canvas */
    auto face = bounds.reduced (1.0f);
    g.setColour (Colour (0xff060609));
    g.fillRoundedRectangle (face, rXs - 0.5f);

    /* Inner shadow superior (box-shadow inset 0 1px 4px rgba(0,0,0,.8)) */
    {
        juce::ColourGradient sh (Colour (0xb3000000), face.getX(), face.getY(),
                                 Colour (0x00000000), face.getX(), face.getY() + 5.0f, false);
        g.setGradientFill (sh);
        g.fillRect (face.getX(), face.getY(), face.getWidth(), 5.0f);
    }

    g.saveState();
    g.reduceClipRegion (face.toNearestInt());

    const float labelW = 68.0f;
    const float barX0  = labelW + 6.0f;
    const float barW   = juce::jmax (10.0f, face.getWidth() - barX0 - 6.0f);
    const float ox     = face.getX(); /* offset por el borde */
    const float oy     = face.getY();

    const float barH   = 11.0f;
    const float gap    = 5.0f;
    const float scaleH = 13.0f;

    float lvl[2] { 0.0f, 0.0f };
    if (provider)
    {
        auto [l, r] = provider();
        lvl[0] = l; lvl[1] = r;
    }

    /* Smoothing fast attack / slow decay sobre dB suavizados */
    const float dbs[2] = { levelToDb (lvl[0]), levelToDb (lvl[1]) };
    for (int i = 0; i < 2; ++i)
    {
        const float t = dbs[i];
        const float c = smooth[i];
        const float k = t > c ? 0.55f : 0.12f;
        smooth[i] = c + (t - c) * k;
    }

    const int segCount = (int) (DB_MAX - DB_MIN); /* 43 segmentos, 1 por dB */
    const float segW = barW / segCount;

    /* Escala dB superior */
    auto scaleFont = font ({ FontId::Mono, 8.5f, 0.0f, false, false });
    g.setFont (scaleFont);
    for (float db : { DB_MIN, -24.0f, -12.0f, -6.0f, 0.0f, DB_MAX })
    {
        const float x = dbToX (db, ox + barX0, barW);
        g.setColour (db >= 0 ? Colour (cDanger).withAlpha (0.85f)
                             : juce::Colours::white.withAlpha (0.50f * 0.915f)); /* e9e9ef≈.5 */
        g.drawText (juce::String ((int) db), (int) x - 20, (int) oy + 4, 40, 10,
                    juce::Justification::centredTop, false);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRect (x - 0.5f, oy + scaleH - 2.0f, 1.0f, 2.0f);
    }

    /* Guard line a 0 dBFS */
    {
        const float zeroX = dbToX (0.0f, ox + barX0, barW);
        juce::Path gl;
        gl.startNewSubPath (zeroX, oy + scaleH);
        gl.lineTo (zeroX, oy + scaleH + 2 * barH + gap);
        juce::PathStrokeType st (1.0f);
        g.setColour (Colour (cDanger).withAlpha (0.4f));
        g.strokePath (gl, st);
    }

    for (int i = 0; i < 2; ++i)
    {
        const float db = smooth[i];
        const float y = oy + scaleH + i * (barH + gap);

        /* Label de canal — en el accent del producto */
        g.setFont (font ({ FontId::Mono, 8.5f, 0.0f, true, false }));
        g.setColour (accentAlpha (0.55f));
        g.drawText (labels[i], (int) ox + 4, (int) y - 5, 60, (int) barH + 10,
                    juce::Justification::centredLeft, false);

        /* Readout dB en vivo */
        g.setFont (font ({ FontId::Mono, 8.5f, 0.0f, true, false }));
        g.setColour (db >= 0 ? Colour (cDanger).withAlpha (0.9f)
                             : juce::Colours::white.withAlpha (0.55f));
        g.drawText (db <= DB_MIN ? "-" + juce::String::charToString ((juce::juce_wchar) 0x221E)
                                 : juce::String (db, 1),
                    (int) (ox + labelW - 64), (int) y - 5, 60, (int) barH + 10,
                    juce::Justification::centredRight, false);

        /* Segmentos: gris neutro todo-bien / ámbar −6..0 / rojo ≥ 0 */
        for (int s = 0; s < segCount; ++s)
        {
            const float segDb = DB_MIN + (float) s;
            const bool lit = db >= segDb;
            const float x = ox + barX0 + s * segW;
            Colour col;
            if (! lit)
                col = juce::Colours::white.withAlpha (0.045f);
            else if (segDb >= 0)
                col = Colour (cDanger).withAlpha (0.95f);
            else if (segDb >= -6)
                col = Colour (cWarn).withAlpha (0.95f);
            else
                col = juce::Colours::white.withAlpha (0.42f); /* gris neutro e9e9ef-ish */
            g.setColour (col);
            g.fillRect (x, y, segW - 0.5f, barH);
        }

        /* Peak hold */
        if (db > peaks[i])
        {
            peaks[i] = db;
            peakTime[i] = 40;
        }
        else
        {
            peakTime[i] = juce::jmax (0, peakTime[i] - 1);
            if (peakTime[i] == 0) peaks[i] -= 0.6f;
        }
        const float pkDb = peaks[i];
        if (pkDb > DB_MIN)
        {
            const float px = dbToX (juce::jmin (DB_MAX, pkDb), ox + barX0, barW);
            g.setColour (pkDb >= 0 ? Colour (cDanger).withAlpha (0.9f)
                                   : juce::Colours::white.withAlpha (0.7f));
            g.fillRect (px - 0.5f, y, 1.5f, barH);
        }
    }

    g.restoreState();

    /* Borde del wrap */
    g.setColour (line());
    g.drawRoundedRectangle (bounds.reduced (0.5f), rXs, 1.0f);
}
} // namespace floodo::brand
