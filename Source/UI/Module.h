#pragma once
#include "Brand.h"
#include "BrandFonts.h"

namespace floodo::brand
{
/* Cabecera de módulo (.module-head): diamante + título display caps +
   hairline que llena el resto. */
class ModuleHead : public juce::Component
{
public:
    explicit ModuleHead (const juce::String& t) : title (t.toUpperCase()) {}

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds();
        const int rowH = 13;

        /* .h-dot — cuadrado 4px rotado 45° */
        g.setColour (lineStrong());
        const float dc = b.getX() + 2.0f, dy = (rowH - 4) / 2.0f;
        juce::Path d;
        d.startNewSubPath (dc + 2, dy);
        d.lineTo (dc + 4, dy + 2);
        d.lineTo (dc + 2, dy + 4);
        d.lineTo (dc, dy + 2);
        d.closeSubPath();
        g.fillPath (d);

        g.setFont (font (tDisplayLG()));
        g.setColour (Colour (textMid));
        g.drawText (title, b.getX() + 10, 0, b.getWidth() - 20, rowH,
                    juce::Justification::centredLeft, false);

        /* ::after — hairline llenando el resto */
        const int textW = (int) textWidth (g.getCurrentFont(), title);
        const int hx = b.getX() + 10 + textW + 14;
        if (hx < b.getRight())
        {
            g.setColour (lineFaint());
            g.fillRect (hx, rowH / 2, b.getRight() - hx, 1);
        }

        /* border-bottom del head */
        g.fillRect (b.getX(), b.getBottom() - 1, b.getWidth(), 1);
    }

private:
    juce::String title;
};

/* Contenedor de módulo (.module): padding 12/18 + ModuleHead arriba. */
class Module : public juce::Component
{
public:
    explicit Module (const juce::String& title) : head (title)
    {
        addAndMakeVisible (head);
        setOpaque (false);
    }

    void resized() override
    {
        head.setBounds (getLocalBounds().removeFromTop (24)); /* 13 texto + pb 10 + borde */
    }

private:
    ModuleHead head;
};
} // namespace floodo::brand
