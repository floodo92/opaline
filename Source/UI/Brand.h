#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace floodo::brand
{
using juce::Colour;
using juce::uint32;

/* ============================================================
   OPALINE — floodo's audio labs Design System (serie free)
   Estética "maison de alta costura": grafito profundo, accent
   ópalo iridiscente (frío → cálido), hairlines finísimas, tipografía
   editorial, cero glow barato. Esto es el MOLDE de la serie free:
   cada plugin conserva el sistema y cambia el accent.
   ============================================================ */

/* ── ACCENT DEL PRODUCTO — la única línea a tocar por producto ──
   Cascade oliva · SignOff oro · OPALINE ópalo iridiscente.
   Ópalo: blanco frío con matiz lavanda/rosa, lee "joyería de lujo". */
inline constexpr uint32 kAccentARGB = 0xffcfc6f2;   /* ópalo iridiscente */

inline Colour accent()            { return Colour(kAccentARGB); }
inline float  accentR()           { return ((kAccentARGB >> 16) & 0xff) / 255.0f; }
inline float  accentG()           { return ((kAccentARGB >> 8) & 0xff) / 255.0f; }
inline float  accentB()           { return (kAccentARGB & 0xff) / 255.0f; }

/* rgba(accent, alpha) — equivalente del helper accent() de Knob.jsx */
inline Colour accentAlpha(float a) { return accent().withAlpha(juce::jlimit(0.0f, 1.0f, a)); }

/* Matices cálidos del ópalo (luces internas, gradientes aire→cuerpo) */
inline Colour opalWarm()  { return Colour(0xffe8c8a8); }  /* ámbar perlado   */
inline Colour opalCold()  { return Colour(0xff9fb8e8); }  /* azul iridiscente */

/* ── Superficies (paleta grafito monocromo, más profunda que Cascade) ── */
inline constexpr uint32 bg          = 0xff060708;
inline constexpr uint32 surface     = 0xff0e1013;
inline constexpr uint32 panel       = 0xff12151a;
inline constexpr uint32 panelMid    = 0xff181b21;
inline constexpr uint32 raised      = 0xff1d2129;
inline constexpr uint32 inset       = 0xff050607;

/* Metal del hero-knob (acero gris medio, no cromo) */
inline Colour metalHi()  { return Colour (0xffa9a9b3); }
inline Colour metalMid() { return Colour (0xff75757f); }
inline Colour metalLo()  { return Colour (0xff484852); }

/* ── Hairlines (blanco al 5.5/9.5/17%) ── */
inline Colour lineFaint()            { return juce::Colours::white.withAlpha(0.055f); }
inline Colour line()                 { return juce::Colours::white.withAlpha(0.095f); }
inline Colour lineStrong()           { return juce::Colours::white.withAlpha(0.17f); }

/* ── Texto ──
   textLo (#5a626c) da 3.0:1 sobre panel: NO alcanza WCAG AA para texto chico.
   textDim (#7b838d) es el escalón para labels secundarios: 4.79:1 sobre panel,
   4.95:1 sobre surface, 5.29:1 sobre inset. textLo queda sólo para texto
   decorativo que no hay que leer. */
inline constexpr uint32 textHi   = 0xffeceef1;
inline constexpr uint32 textMid  = 0xff9aa1ab;
inline constexpr uint32 textDim  = 0xff7b838d;
inline constexpr uint32 textLo   = 0xff5a626c;

/* ── Señales semánticas — SOLO meters/data-viz y acciones peligrosas ── */
inline constexpr uint32 cSafe    = 0xff52c49a;
inline constexpr uint32 cWarn    = 0xffe0ad5a;
inline constexpr uint32 cDanger  = 0xe06a6aff;

/* ── Radios ── */
inline constexpr float rXs = 3.0f;
inline constexpr float rSm = 5.0f;
inline constexpr float rMd = 8.0f;
inline constexpr float rLg = 10.0f;

/* ── Canvas fijo de diseño (la UI escala como unidad) ──
   Rediseño compacto "one-knob" tipo free: cuadrado, chico, con todos los
   controles. Opaline ya no es el landscape 1000×560 de la serie Cascade. */
inline constexpr int canvasW = 400;
inline constexpr int canvasH = 460;

/* Chamfer de la familia keycap (clip-path 6px en las esquinas inferiores) */
inline constexpr float keycapChamfer = 6.0f;

/* ============================================================
   Tipografía
   Inter            → UI / etiquetas funcionales
   Barlow Condensed → display: títulos, labels, MAYÚSCULAS con tracking
   JetBrains Mono   → valores y readouts (tabular)
   ============================================================ */
enum class FontId { Inter, Barlow, Mono };

struct Type
{
    FontId id = FontId::Inter;
    float height = 11.5f;
    float kerning = 0.0f;
    bool bold = false;
    bool uppercase = false;
    /* 400/500/600/700 explícito. 0 = peso por defecto de la familia.
       Sólo Barlow tiene pesos estáticos reales (500/600/700): Inter y Mono
       son fuentes variables con default wght=400, así que ahí el peso no
       cambia el render (ver BrandFonts.cpp). */
    int weight = 0;
};

inline Type tDisplayXL()  { return { FontId::Barlow, 22.0f, 0.16f, true,  true  }; } /* hero-title   */
inline Type tDisplayLG()  { return { FontId::Barlow, 12.0f, 0.24f, false, true  }; } /* module-head  */
inline Type tDisplayMD()  { return { FontId::Barlow, 10.0f, 0.16f, false, true  }; } /* sublabels    */
inline Type tDisplaySM()  { return { FontId::Barlow, 9.0f,  0.18f, false, true  }; } /* eyebrow      */
inline Type tUI()         { return { FontId::Inter,  11.5f, 0.0f,  false, false }; }
inline Type tUISm()       { return { FontId::Inter,  11.0f, 0.0f,  false, false }; }
inline Type tMono()       { return { FontId::Mono,   11.5f, 0.01f, false, false }; }
inline Type tMonoSm8()    { return { FontId::Mono,   8.5f,  0.0f,  false, false }; }

inline float textWidth (const juce::Font& f, const juce::String& s)
{ return juce::GlyphArrangement::getStringWidth (f, s); }

} // namespace floodo::brand

