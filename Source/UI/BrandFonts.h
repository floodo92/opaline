#pragma once
#include "Brand.h"

namespace floodo::brand
{
/* Typefaces embebidos (BinaryData) con caché estática.
   Peso más cercano disponible: Inter 400/500/600/700, Barlow 500/600/700,
   Mono 400/500/600. `bold` en Type elige el peso display de cada familia. */
juce::Font font(const Type& t);
}
