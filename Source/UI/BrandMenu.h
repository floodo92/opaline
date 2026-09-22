#pragma once
#include "Brand.h"
#include "BrandFonts.h"

namespace floodo::brand
{
/* LookAndFeel del popup de marca (.dd-panel / .dd-opt): fondo raised, borde
   hairline-strong, group labels Barlow caps tracking, items con dot de accent
   para el seleccionado. Común al menú contextual de los knobs. */
struct BrandMenuLNF : public juce::LookAndFeel_V4
{
    void drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int w, int h,
                                             const juce::PopupMenu::Options&) override
    {
        auto b = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (Colour (raised));
        g.fillRoundedRectangle (b, rMd);
        g.setColour (lineStrong());
        g.drawRoundedRectangle (b, rMd, 1.0f);
    }

    int getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&) override { return 5; }

    void drawPopupMenuSectionHeaderWithOptions (juce::Graphics& g,
                                                const juce::Rectangle<int>& area,
                                                const juce::String& sectionName,
                                                const juce::PopupMenu::Options&) override
    {
        g.setFont (font ({ FontId::Barlow, 10.0f, 0.22f, true, true }));
        g.setColour (Colour (textLo));
        g.drawText (sectionName.toUpperCase(), area.getX() + 10, area.getY(),
                    area.getWidth() - 12, area.getHeight(),
                    juce::Justification::centredLeft, false);
    }

    void drawPopupMenuItemWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area,
                                       bool isHighlighted, const juce::PopupMenu::Item& item,
                                       const juce::PopupMenu::Options&) override
    {
        auto b = area.toFloat().reduced (2.0f, 0.5f);
        const bool selected = item.colour == accent();

        if (selected)
        {
            g.setColour (accentAlpha (0.12f));
            g.fillRoundedRectangle (b, rXs);
        }
        else if (isHighlighted)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (b, rXs);
        }

        const float dotX = b.getX() + 10.0f;
        if (selected)
        {
            g.setColour (accent());
            g.fillEllipse (dotX - 2.0f, b.getCentreY() - 2.0f, 4.0f, 4.0f);
        }

        g.setFont (font (tUI()));
        g.setColour (selected ? accent()
                              : (isHighlighted ? Colour (textHi) : Colour (textMid)));
        g.drawText (item.text, (int) dotX + 8, area.getY(), area.getWidth() - 20,
                    area.getHeight(), juce::Justification::centredLeft, true);
    }
};
} // namespace floodo::brand