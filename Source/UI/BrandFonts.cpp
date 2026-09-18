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

    /* Peso explícito si se pidió; si no, el default de la marca:
       Inter 500 / Barlow 600 / Mono 600, y bold sube al 700.
       OJO: Inter y Mono son fuentes VARIABLES con wght default 400 —
       createSystemTypefaceFor devuelve esa instancia, así que 400/500/600/700
       se ven iguales. Sólo Barlow (estática) respeta el peso. */
    Typeface::Ptr tf;
    if      (t.weight >= 700) tf = fam->bold;
    else if (t.weight >= 600) tf = fam->semibold;
    else if (t.weight >= 500) tf = fam->medium;
    else if (t.weight >= 400) tf = fam->regular;
    else tf = t.bold ? fam->bold
                     : (t.id == FontId::Inter ? fam->medium : fam->semibold);
    if (tf == nullptr) tf = fam->regular;

    /* OJO: withHeight() fija (ascent + descent), NO el em box. En Barlow/Inter/
       Mono esa suma ronda 1.2, así que un height de 18 daba glyphs de ~10.5px
       en vez de 12.6: todo el texto salía ~17% más chico que el diseño, que
       está expresado en px CSS (= puntos). withPointHeight() es la API correcta:
       el valor que se pasa es el em box, igual que font-size en CSS. */
    auto options = juce::FontOptions().withTypeface (tf).withPointHeight (t.height);
    auto f = juce::Font(options);
    f.setExtraKerningFactor(t.kerning);
    return f;
}
} // namespace floodo::brand
