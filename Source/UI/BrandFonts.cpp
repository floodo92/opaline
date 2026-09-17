#include "BrandFonts.h"
#include "BinaryData.h"
#include <array>

namespace floodo::brand
{
namespace
{
using juce::Typeface;

struct Family
{
    Typeface::Ptr regular, medium, semibold, bold;
};

Family loadInter()
{
    return { Typeface::createSystemTypefaceFor(BinaryData::Inter400_ttf, BinaryData::Inter400_ttfSize),
             Typeface::createSystemTypefaceFor(BinaryData::Inter500_ttf, BinaryData::Inter500_ttfSize),
             Typeface::createSystemTypefaceFor(BinaryData::Inter600_ttf, BinaryData::Inter600_ttfSize),
             Typeface::createSystemTypefaceFor(BinaryData::Inter700_ttf, BinaryData::Inter700_ttfSize) };
}

Family loadBarlow()
{
    return { Typeface::createSystemTypefaceFor(BinaryData::Barlow500_ttf, BinaryData::Barlow500_ttfSize),
             Typeface::createSystemTypefaceFor(BinaryData::Barlow600_ttf, BinaryData::Barlow600_ttfSize),
             Typeface::createSystemTypefaceFor(BinaryData::Barlow700_ttf, BinaryData::Barlow700_ttfSize),
             Typeface::createSystemTypefaceFor(BinaryData::Barlow700_ttf, BinaryData::Barlow700_ttfSize) };
}

Family loadMono()
{
    return { Typeface::createSystemTypefaceFor(BinaryData::Mono400_ttf, BinaryData::Mono400_ttfSize),
             Typeface::createSystemTypefaceFor(BinaryData::Mono500_ttf, BinaryData::Mono500_ttfSize),
             Typeface::createSystemTypefaceFor(BinaryData::Mono600_ttf, BinaryData::Mono600_ttfSize),
             Typeface::createSystemTypefaceFor(BinaryData::Mono600_ttf, BinaryData::Mono600_ttfSize) };
}

} // namespace

juce::Font font(const Type& t)
{
    static const Family inter  = loadInter();
    static const Family barlow = loadBarlow();
    static const Family mono   = loadMono();

    const Family* fam = (t.id == FontId::Inter) ? &inter : (t.id == FontId::Barlow) ? &barlow : &mono;

    /* Peso por defecto de la marca: 500/600 según familia; bold sube al 700. */
    Typeface::Ptr tf = t.bold ? fam->bold
                              : (t.id == FontId::Inter ? fam->medium : fam->semibold);
    if (tf == nullptr) tf = fam->regular;

    auto options = juce::FontOptions().withTypeface(tf).withHeight(t.height);
    auto f = juce::Font(options);
    f.setExtraKerningFactor(t.kerning);
    return f;
}
} // namespace floodo::brand
