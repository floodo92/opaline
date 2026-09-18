#include "Knob.h"
#include "BrandFonts.h"

namespace floodo::brand
{
static constexpr double kStartDeg = 120.0;   /* START_DEG de Knob.jsx */
static constexpr double kArcDeg   = 300.0;   /* ARC_DEG */
static inline double toRad (double deg)      { return deg * juce::MathConstants<double>::pi / 180.0; }
static const double kStartRad = toRad (kStartDeg);
static const double kEndRad   = toRad (kStartDeg + kArcDeg);

static inline double normToAngle (double norm) { return kStartRad + norm * (kEndRad - kStartRad); }

/* Stroke helper con cap redondeado (canvas lineCap:'round') */
static void strokeRoundedPath (juce::Graphics& g, const juce::Path& p, float width, Colour c)
{
    juce::PathStrokeType st (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (c);
    g.strokePath (p, st);
}

static void strokeButtPath (juce::Graphics& g, const juce::Path& p, float width, Colour c)
{
    juce::PathStrokeType st (width, juce::PathStrokeType::mitered, juce::PathStrokeType::butt);
    g.setColour (c);
    g.strokePath (p, st);
}

/* Arco entre dos ángulos (convención CANVAS: 0=+x, horario, y-down)
   inscrito en el círculo de centro (cx,cy) radio r.
   OJO: Path::addCentredArc mide ángulos desde las 12 en punto — convertir
   sumando π/2 para que coincida con el puntero/ticks dibujados a mano. */
static void addCenterArc (juce::Path& p, float cx, float cy, float r,
                          double fromRad, double toRad)
{
    constexpr double off = juce::MathConstants<double>::pi / 2.0;
    p.addCentredArc (cx, cy, r, r, 0.0f,
                     (float) (fromRad + off), (float) (toRad + off), true);
}

static void radialLine (juce::Path& p, float cx, float cy, double a, float r1, float r2)
{
    p.startNewSubPath (cx + std::cos (a) * r1, cy + std::sin (a) * r1);
    p.lineTo         (cx + std::cos (a) * r2, cy + std::sin (a) * r2);
}

/*============================================================================*/

Knob::Knob (float knobSize)
    : size (knobSize), pad (8.0f)
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor); /* ns-resize */
    setOpaque (false);
}

void Knob::setValue (double v, bool notify)
{
    v = snapIfNeeded (v);
    v = juce::jlimit (min, max, v);
    if (juce::exactlyEqual (v, value)) return;
    value = v;
    repaint();
    if (onValueChanged) onValueChanged();
    if (notify && onChange) onChange (value);
}

void Knob::setRange (double newMin, double newMax)
{
    min = newMin; max = newMax;
    setValue (value, false);
}

void Knob::setDefaultValue (double v)
{
    defaultValue = v;
    repaint();
}

double Knob::clampNorm (double n) const { return juce::jlimit (0.0, 1.0, n); }

double Knob::snapIfNeeded (double raw) const
{
    if (steps <= 1) return raw;
    const double norm = clampNorm ((raw - min) / (max - min));
    const double snapped = std::round (norm * (steps - 1)) / (steps - 1);
    return min + snapped * (max - min);
}

juce::String Knob::displayText() const
{
    if (formatValue) return formatValue (value);
    return juce::String ((int) std::round (value)) + "%";
}

void Knob::resized()
{
    /* El padre posiciona con totalHeight() — nada que hacer acá. */
}

void Knob::paint (juce::Graphics& g)
{
    const float  cw = knobDiameter();
    const float  ch = knobDiameter();
    const float  cx = cw / 2.0f;
    const float  cy = ch / 2.0f;
    const double norm       = clampNorm ((value - min) / (max - min));
    const double valueAngle = normToAngle (norm);

    if (finish == Finish::Precision)
        paintPrecisionDial (g, cx, cy, valueAngle, norm);
    else
        paintBrandDial (g, cx, cy, valueAngle, norm);

    paintText (g, cw, ch);
}

/*============================================================================*/

void Knob::paintBrandDial (juce::Graphics& g, float cx, float cy,
                           double valueAngle, double norm)
{
    const bool   active     = isActive();

    const float trackR = size / 2.0f - 4.0f;
    const float dialR  = trackR - 5.0f;
    const float sx     = cx + dialR * -0.35f;
    const float sy     = cy + dialR * -0.38f;

    /* ── Cast shadow (soft, offset down-right) ── */
    {
        juce::ColourGradient sh (Colour (0x61000000), cx + 2, cy + 3,
                                 Colour (0x00000000), cx + 2, cy + 3, true);
        sh.addColour (0.0, Colour (0x61000000));
        sh.addColour (1.0, Colour (0x00000000));
        g.setGradientFill (sh);
        g.fillEllipse (cx + 2 - dialR * 1.05f, cy + 3 - dialR * 1.05f,
                       dialR * 2.10f, dialR * 2.10f);
    }

    /* ── Outer precision ring (grabado, panel, fijo) ── */
    {
        juce::Path ring;
        addCenterArc (ring, cx, cy, trackR + 3.5f, kStartRad, kEndRad);
        strokeButtPath (g, ring, 1.0f, Colour (0x80000000));
    }

    /* ── Escala grabada: 25 ticks, mayor cada 4 ── */
    for (int m = 0; m <= 24; ++m)
    {
        const double n = m / 24.0;
        const bool isMaj = std::abs (n * 6.0 - std::round (n * 6.0)) < 0.001;
        const double a = normToAngle (n);
        const float r1 = isMaj ? trackR - 4.5f : trackR - 2.5f;
        const float r2 = trackR + (isMaj ? 3.0f : 1.8f);
        juce::Path t;
        radialLine (t, cx, cy, a, r1, r2);
        strokeButtPath (g, t, isMaj ? 1.3f : 0.9f,
                        juce::Colours::white.withAlpha (isMaj ? 0.40f : 0.14f));
    }

    /* ── Detent ticks para switches por pasos ── */
    if (steps > 1)
    {
        for (int i = 0; i < steps; ++i)
        {
            const double n = i / (double) (steps - 1);
            const double a = normToAngle (n);
            const bool on = std::abs (n - norm) < 0.012;
            juce::Path t;
            radialLine (t, cx, cy, a, trackR - 4.0f, trackR + 3.0f);
            strokeButtPath (g, t, 1.2f, on ? accentAlpha (0.95f)
                                           : juce::Colours::white.withAlpha (0.18f));
        }
    }

    /* ── Labels de escala alrededor ── */
    if (scaleLabels.size() > 1)
    {
        auto f = font ({ FontId::Mono, 8.0f, 0.0f, false, false });
        f.setHorizontalScale (1.0f);
        g.setFont (f);
        g.setColour (juce::Colours::white.withAlpha (0.32f));
        const float lr = juce::jmin (trackR + 9.5f, size / 2.0f + pad - 5.5f);
        for (int i = 0; i < scaleLabels.size(); ++i)
        {
            const double n = i / (double) (scaleLabels.size() - 1);
            const double a = normToAngle (n);
            const float tx = cx + (float) std::cos (a) * lr;
            const float ty = cy + (float) std::sin (a) * lr;
            const auto txt = scaleLabels.getReference (i);
            g.drawText (txt,
                        (int) (tx - 20), (int) (ty - 6), 40, 12,
                        juce::Justification::centred, false);
        }
    }

    /* ── Marcador de posición default ── */
    if (defaultValue > min - 1e-9 && steps == 0)
    {
        const double defNorm = clampNorm ((defaultValue - min) / (max - min));
        const double defAngle = normToAngle (defNorm);
        const bool nearDef = std::abs (norm - defNorm) < 0.006;
        juce::Path t;
        radialLine (t, cx, cy, defAngle, trackR - 4.0f, trackR + 2.5f);
        strokeButtPath (g, t, 1.0f, juce::Colours::white.withAlpha (nearDef ? 0.55f : 0.20f));
    }

    /* ── Zero tick bipolar ── */
    if (bipolar)
    {
        const double za = normToAngle (clampNorm ((0.0 - min) / (max - min)));
        juce::Path t;
        radialLine (t, cx, cy, za, trackR - 4.0f, trackR + 2.5f);
        strokeButtPath (g, t, 1.0f, juce::Colours::white.withAlpha (0.18f));
    }

    /* ── Value arc (bezel ring fuera de la banda de ticks) ──
       Unipolar: desde el mínimo hasta el valor.
       Bipolar:  anclado al CERO — crece hacia el lado del valor, invisible en 0.
       Selector: sector alrededor del detent activo (sigue al indicador). */
    {
        juce::Path base;
        addCenterArc (base, cx, cy, trackR + 4.0f, kStartRad, kEndRad);
        strokeRoundedPath (g, base, 2.0f, juce::Colours::white.withAlpha (0.10f));

        double arcFrom = kStartRad, arcTo = valueAngle;
        bool   drawArc = true;

        if (steps > 1)
        {
            const double half = 0.5 / (double) (steps - 1);
            const double n = clampNorm ((value - min) / (max - min));
            arcFrom = normToAngle (juce::jlimit (0.0, 1.0, n - half));
            arcTo   = normToAngle (juce::jlimit (0.0, 1.0, n + half));
        }
        else if (bipolar)
        {
            const double zn = clampNorm ((0.0 - min) / (max - min));
            if (std::abs (norm - zn) < 0.003)
                drawArc = false;                      /* en cero: sin halo */
            else if (norm > zn) { arcFrom = normToAngle (zn); arcTo = valueAngle; }
            else                { arcFrom = valueAngle; arcTo = normToAngle (zn); }
        }

        if (drawArc && arcTo > arcFrom + 0.01)
        {
            juce::Path val;
            addCenterArc (val, cx, cy, trackR + 4.0f, arcFrom, arcTo);
            strokeRoundedPath (g, val, 2.0f, accentAlpha (active ? 1.0f : 0.72f));
        }
    }

    /* ── Índice fijo del bezel — a las 12 ── */
    {
        const double idxA = normToAngle (0.5);
        g.setColour (accentAlpha (0.9f));
        g.fillEllipse (cx + (float) std::cos (idxA) * (trackR + 4.0f) - 1.7f,
                       cy + (float) std::sin (idxA) * (trackR + 4.0f) - 1.7f,
                       3.4f, 3.4f);
    }

    /* ── Cara del dial — grafito abombado o metal pulido ── */
    if (finish == Finish::Metallic)
    {
        /* Metal: gradiente diagonal hi→mid→lo (plano, poco contraste) */
        juce::ColourGradient face (metalHi(), cx - dialR * 0.55f, cy - dialR * 0.80f,
                                   metalLo(), cx + dialR * 0.60f, cy + dialR * 0.90f, false);
        face.addColour (0.55, metalMid());
        g.setGradientFill (face);
        g.fillEllipse (cx - dialR, cy - dialR, dialR * 2.0f, dialR * 2.0f);

        /* Cepillado radial sutil (plástico no, metal) */
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (juce::Rectangle<float> (cx - dialR, cy - dialR,
                                                         dialR * 2.0f, dialR * 2.0f).toNearestInt());
            for (int i = 0; i < 90; ++i)
            {
                if ((i & 1) == 0) continue;
                const double a = i / 90.0 * juce::MathConstants<double>::twoPi;
                juce::Path br;
                br.startNewSubPath (cx, cy);
                br.lineTo (cx + (float) std::cos (a) * dialR,
                           cy + (float) std::sin (a) * dialR);
                g.setColour (juce::Colours::white.withAlpha (0.022f));
                g.strokePath (br, juce::PathStrokeType (1.0f));
            }
        }

        /* Specular superior-izquierda */
        {
            juce::ColourGradient sp (juce::Colours::white.withAlpha (0.30f),
                                     cx - dialR * 0.42f, cy - dialR * 0.58f,
                                     juce::Colours::white.withAlpha (0.0f),
                                     cx - dialR * 0.42f, cy - dialR * 0.58f, true);
            sp.clearColours();
            sp.addColour (0.0, juce::Colours::white.withAlpha (0.30f));
            sp.addColour (1.0, juce::Colours::white.withAlpha (0.0f));
            g.setGradientFill (sp);
            g.fillEllipse (cx - dialR * 0.74f, cy - dialR * 0.88f,
                           dialR * 1.18f, dialR * 0.88f);
        }

        juce::Path rim;
        rim.addEllipse (cx - dialR + 0.6f, cy - dialR + 0.6f,
                        (dialR - 0.6f) * 2.0f, (dialR - 0.6f) * 2.0f);
        strokeButtPath (g, rim, 1.2f, Colour (0x59000000));
    }
    else
    {
        juce::ColourGradient face (Colour (0xff32323d), sx, sy,
                                   Colour (0xff191922), cx, cy, true);
        face.addColour (0.50, Colour (0xff24242e));
        face.addColour (1.00, Colour (0xff191922));
        g.setGradientFill (face);
        g.fillEllipse (cx - dialR, cy - dialR, dialR * 2.0f, dialR * 2.0f);

        /* Rim hairline — separa el cuerpo del panel */
        juce::Path rim;
        rim.addEllipse (cx - dialR + 0.5f, cy - dialR + 0.5f,
                        (dialR - 0.5f) * 2.0f, (dialR - 0.5f) * 2.0f);
        strokeButtPath (g, rim, 1.0f, juce::Colours::white.withAlpha (0.16f));
    }

    /* ── Faceta maquinada rotatoria — bisel que sigue al puntero ── */
    {
        juce::Path facet;
        facet.addCentredArc (cx, cy, dialR, dialR, 0.0f,
                             (float) (valueAngle - juce::MathConstants<double>::pi / 2.0),
                             (float) (valueAngle + juce::MathConstants<double>::pi / 2.0),
                             true);
        facet.closeSubPath(); /* chord semicircular, igual que ctx.closePath() */
        g.setColour (juce::Colours::white.withAlpha (0.09f));
        g.fillPath (facet);
    }

    /* ── Retícula maquinada — dos ranuras cruzadas que rotan ── */
    {
        const float slotLen = dialR * 0.66f;
        for (const double ang : { valueAngle, valueAngle + juce::MathConstants<double>::pi / 2.0 })
        {
            juce::Path slot;
            slot.startNewSubPath (cx + (float) std::cos (ang) * slotLen,
                                  cy + (float) std::sin (ang) * slotLen);
            slot.lineTo (cx - (float) std::cos (ang) * slotLen,
                         cy - (float) std::sin (ang) * slotLen);
            strokeRoundedPath (g, slot, 2.4f, Colour (0x73000000));
        }
    }

    /* ── Bevel de aro (hairlines luz/sombra) ── */
    {
        juce::Path light;
        addCenterArc (light, cx, cy, dialR - 0.5f, toRad (205.0), toRad (335.0));
        strokeButtPath (g, light, 1.1f, juce::Colours::white.withAlpha (0.24f));

        juce::Path dark;
        addCenterArc (dark, cx, cy, dialR - 0.5f, toRad (25.0), toRad (155.0));
        strokeButtPath (g, dark, 1.1f, Colour (0x8c000000));
    }

    /* ── Puntero lanza blanco con chaflán y perfil estrangulado ── */
    {
        const float pIn   = dialR * 0.16f;
        const float waist = dialR * 0.62f;
        const float pOut  = dialR * 0.92f;
        const float bHalf = dialR * 0.17f;
        const float wHalf = dialR * 0.075f;
        const double va   = valueAngle;
        const float ca = (float) std::cos (va), sa = (float) std::sin (va);
        const float nx = -sa, ny = ca;

        juce::Path blade;
        blade.startNewSubPath (cx + ca * pIn   + nx * bHalf, cy + sa * pIn   + ny * bHalf);
        blade.lineTo          (cx + ca * waist + nx * wHalf, cy + sa * waist + ny * wHalf);
        blade.lineTo          (cx + ca * pOut,               cy + sa * pOut);
        blade.lineTo          (cx + ca * waist - nx * wHalf, cy + sa * waist - ny * wHalf);
        blade.lineTo          (cx + ca * pIn   - nx * bHalf, cy + sa * pIn   - ny * bHalf);
        blade.closeSubPath();

        g.setColour (finish == Finish::Metallic
                        ? Colour (active ? 0xff1b1b24 : 0xff2c2c37)
                        : (active ? Colour (0xffffffff) : Colour (0xebf0f0f4)));
        g.fillPath (blade);
        juce::PathStrokeType st (0.7f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt);
        g.setColour (finish == Finish::Metallic ? Colour (0x33ffffff)
                                                : Colour (0x59000000));
        g.strokePath (blade, st);
    }

    /* ── Sello central: anillo accent + núcleo oscuro + punto accent ── */
    {
        const float hubR = juce::jmax (2.2f, dialR * 0.14f);
        juce::Path hubRing;
        hubRing.addEllipse (cx - hubR - 2.2f, cy - hubR - 2.2f, (hubR + 2.2f) * 2, (hubR + 2.2f) * 2);
        strokeButtPath (g, hubRing, 1.1f, accentAlpha (0.6f));

        g.setColour (Colour (0xff0a0a0f));
        g.fillEllipse (cx - hubR, cy - hubR, hubR * 2, hubR * 2);

        g.setColour (accentAlpha (0.55f));
        g.fillEllipse (cx - hubR * 0.45f, cy - hubR * 0.45f, hubR * 0.9f, hubR * 0.9f);
    }

    /* ── Sheen hover/drag (accent 9% → 0) ── */
    if (active)
    {
        juce::ColourGradient sheen (accentAlpha (0.09f), cx, cy,
                                    accentAlpha (0.0f), cx, cy, true);
        sheen.clearColours();
        sheen.addColour (0.0, accentAlpha (0.09f));
        sheen.addColour (1.0, accentAlpha (0.0f));
        g.setGradientFill (sheen);
        g.fillEllipse (cx - dialR, cy - dialR, dialR * 2.0f, dialR * 2.0f);
    }

}

/* ── Readout (.knob-val) + label (.knob-lbl) debajo del canvas ──
   Con showReadout=false el texto lo dibuja el padre (layout Precision, que
   necesita label ARRIBA del valor y tipografías que no son las de la serie). */
void Knob::paintText (juce::Graphics& g, float cw, float ch)
{
    const bool active = isActive();
    int y = (int) ch + rowGap;

    if (showReadout)
    {
        const bool revealReadout = ! (readoutReveal && ! active);
        if (revealReadout)
        {
            g.setFont (font (tMono()));
            g.setColour (active ? accent() : Colour (textHi));
            g.drawText (displayText(), 0, y, (int) cw, readoutH,
                        juce::Justification::centredTop, false);
        }
        y += readoutH;
    }

    if (showLabel && labelText.isNotEmpty())
    {
        g.setFont (font (tDisplayMD()));
        g.setColour (active ? juce::Colours::white.withAlpha (0.5f) : Colour (textDim));
        g.drawText (labelText, 0, y, (int) cw, labelH,
                    juce::Justification::centredTop, false);
    }
}

/*============================================================================*/

/* Dial "instrumento de precision" para Opaline — PORT 1:1 de
   opaline_design.html (la fuente de verdad aprobada). Capas, en orden:

     .knob-hero / .knob-small          pocillo  : radial 34%/30% (hero)
                                                 36%/32% (chico)
                                                 #242831 -> #1d2129 -> #15181e
     .knob-hero-metal / -small-metal   aro acero: linear 180deg
                                                 #a9a9b3 -> #75757f -> #484852
                                                 + luz radial encima
     .knob-hero-inset / -small-inset   cara     : radial 38%/32% (hero)
                                                 38%/34% (chico)
                                                 #22252e -> #1d2129 -> #13161c
     .knob-center-mark                 marca 12h del cero bipolar (solo bipolar)
     .knob-hero-indicator              aguja BLANCA 2px del centro al borde,
                                       cap redondo; ::after (solo hero activo)
                                       tine los 24px exteriores con el opalo
     .knob-hero-dot / .knob-small-dot  punto #0e1013 con hairline, ENCIMA

   Geometria del mockup (px CSS sobre el canvas): el hero mide 156 (wellR 78,
   metal 67, inset 64, aguja 54, dot 6) y el chico 72 (wellR 36, metal 30,
   inset 28, aguja 26, dot 4). Los dos knobs del plugin tienen exactamente esos
   diametros, asi que se usan los valores absolutos del diseno.
   NO hay ticks, arco de valor, bisel ni sello central: la posicion la lee la
   aguja, que es lo que hace que lea instrumento y no "plugin generico". */
void Knob::paintPrecisionDial (juce::Graphics& g, float cx, float cy,
                              double valueAngle, double norm)
{
    juce::ignoreUnused (norm);

    const bool  active = isActive();
    const float dia    = knobDiameter();
    const float wellR  = dia * 0.5f;
    const bool  hero   = dia > 100.0f;

    /* Geometria exacta del mockup (px CSS). */
    const float metalR = hero ? 67.0f : 30.0f;
    const float insetR = hero ? 64.0f : 28.0f;
    const float ptrLen = hero ? 54.0f : 26.0f;
    const float dotR   = hero ? 3.0f  : 2.0f;

    /* ── 1. Pocillo: radial 34%/30% (hero) · 36%/32% (chico) ──
       El radio del gradiente es `farthest-corner` de CSS: 1.9241*wellR en el
       hero, 1.8678*wellR en el chico. */
    {
        const float ox = hero ? -0.32f : -0.28f;
        const float oy = hero ? -0.40f : -0.36f;
        const float gr = wellR * (hero ? 1.9241f : 1.8678f);
        juce::ColourGradient well (Colour (0xff242831), cx + ox * wellR, cy + oy * wellR,
                                   Colour (0xff15181e), cx + ox * wellR + gr, cy + oy * wellR, true);
        well.addColour (hero ? 0.48f : 0.55f, Colour (0xff1d2129));
        g.setGradientFill (well);
        g.fillEllipse (cx - wellR, cy - wellR, wellR * 2.0f, wellR * 2.0f);

        g.setColour (line());   /* border 1px rgba(255,255,255,0.095) */
        g.drawEllipse (cx - wellR + 0.5f, cy - wellR + 0.5f,
                       (wellR - 0.5f) * 2.0f, (wellR - 0.5f) * 2.0f, 1.0f);
    }

    /* ── 2. Aro de acero: dos fills (linear 180deg + luz radial) ──
       Visible solo 3px (hero) / 2px (chico) porque la cara lo tapa casi todo. */
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path disc;
        disc.addEllipse (cx - metalR, cy - metalR, metalR * 2.0f, metalR * 2.0f);
        g.reduceClipRegion (disc, {});

        juce::ColourGradient metal (metalHi(), cx, cy - metalR,
                                    metalLo(), cx, cy + metalR, false);
        metal.addColour (hero ? 0.52f : 0.50f, metalMid());
        g.setGradientFill (metal);
        g.fillPath (disc);

        const float ox = hero ? -0.32f : -0.28f;
        const float oy = hero ? -0.44f : -0.40f;
        const float gr = metalR * (hero ? 1.9534f : 1.8970f);
        juce::ColourGradient light (juce::Colours::white.withAlpha (hero ? 0.30f : 0.28f),
                                    cx + ox * metalR, cy + oy * metalR,
                                    juce::Colours::white.withAlpha (0.0f),
                                    cx + ox * metalR + gr, cy + oy * metalR, true);
        light.addColour (hero ? 0.34f : 0.36f,
                         juce::Colours::white.withAlpha (hero ? 0.07f : 0.06f));
        light.addColour (hero ? 0.58f : 0.60f, juce::Colours::white.withAlpha (0.0f));
        g.setGradientFill (light);
        g.fillPath (disc);
    }

    /* ── 3. Cara oscura: radial 38%/32% (hero) · 38%/34% (chico) ── */
    {
        const float ox = -0.24f;
        const float oy = hero ? -0.36f : -0.32f;
        const float gr = insetR * (hero ? 1.8405f : 1.8110f);
        juce::ColourGradient face (Colour (0xff22252e), cx + ox * insetR, cy + oy * insetR,
                                   Colour (0xff13161c), cx + ox * insetR + gr, cy + oy * insetR, true);
        face.addColour (hero ? 0.60f : 0.62f, Colour (0xff1d2129));
        g.setGradientFill (face);
        g.fillEllipse (cx - insetR, cy - insetR, insetR * 2.0f, insetR * 2.0f);

        g.setColour (lineFaint());   /* border 1px rgba(255,255,255,0.055) */
        g.drawEllipse (cx - insetR + 0.5f, cy - insetR + 0.5f,
                       (insetR - 0.5f) * 2.0f, (insetR - 0.5f) * 2.0f, 1.0f);
    }

    /* ── 4. Marca del cero bipolar: hairline vertical a las 12h sobre el aro ──
       (.knob-center-mark: 1x8px, top:4px en el wrap de 72 -> radio 24..32) */
    if (bipolar)
    {
        juce::Path t;
        radialLine (t, cx, cy, normToAngle (0.5), insetR - 4.0f, metalR + 2.0f);
        strokeButtPath (g, t, 1.0f, juce::Colours::white.withAlpha (0.17f));
    }

    /* ── 5. Aguja blanca: del centro al borde, 2px, cap redondo ── */
    {
        const float ca = (float) std::cos (valueAngle), sa = (float) std::sin (valueAngle);
        juce::Path needle;
        needle.startNewSubPath (cx, cy);
        needle.lineTo (cx + ca * ptrLen, cy + sa * ptrLen);
        strokeRoundedPath (g, needle, 2.0f,
                           hero ? juce::Colours::white.withAlpha (0.90f)
                                : Colour (0xffeceef1).withAlpha (0.92f));

        /* ::after — solo `.knob-hero-wrap.is-active`: tramo EXTERIOR de 24px
           teñido con el opalo frio->accent->calido al 60% de opacidad. */
        if (hero && active)
        {
            const float r0 = ptrLen - 24.0f;   /* 54 -> 30 */
            juce::Path tip;
            tip.startNewSubPath (cx + ca * r0, cy + sa * r0);
            tip.lineTo (cx + ca * ptrLen, cy + sa * ptrLen);
            juce::ColourGradient op (opalCold().withAlpha (0.6f), cx + ca * r0, cy + sa * r0,
                                     opalWarm().withAlpha (0.6f), cx + ca * ptrLen, cy + sa * ptrLen,
                                     false);
            op.addColour (0.55f, accent().withAlpha (0.6f));
            g.setGradientFill (op);
            g.strokePath (tip, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }
    }

    /* ── 6. Punto central (#0e1013 + hairline), ENCIMA de la aguja ── */
    {
        g.setColour (Colour (surface));
        g.fillEllipse (cx - dotR, cy - dotR, dotR * 2.0f, dotR * 2.0f);
        g.setColour (line());
        g.drawEllipse (cx - dotR, cy - dotR, dotR * 2.0f, dotR * 2.0f, 1.0f);
    }
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    dragStartY = e.position.y;
    dragStartVal = value;
    repaint();
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float sens = e.mods.isShiftDown() ? 0.15f : 1.0f;
    const double arcLen = (kArcDeg / 360.0) * juce::MathConstants<double>::twoPi * (size / 2.0);
    const double delta = (dragStartY - e.position.y) * sens * ((max - min) / arcLen);
    setValue (dragStartVal + delta);
}

void Knob::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    repaint();
}

void Knob::mouseEnter (const juce::MouseEvent&) { hovered = true;  repaint(); }
void Knob::mouseExit (const juce::MouseEvent&) { hovered = false; repaint(); }

void Knob::mouseDoubleClick (const juce::MouseEvent&)
{
    setValue (defaultValue > min - 1e300 ? defaultValue : (min + max) / 2.0);
}

bool Knob::keyPressed (const juce::KeyPress&)
{
    return false;
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int dir = wheel.deltaY > 0.0f ? 1 : -1; /* scroll up = subir valor */
    if (steps > 1)
    {
        const double stepSize = (max - min) / (steps - 1);
        setValue (value + dir * stepSize);
    }
    else
    {
        const double denom = e.mods.isShiftDown() ? 1000.0 : 100.0; /* Shift = fino */
        setValue (value + dir * (max - min) / denom);
    }
}

/*============================================================================*/

KnobAttachment::KnobAttachment (juce::AudioProcessorValueTreeState& state,
                                const juce::String& paramID,
                                Knob& knobToUpdate)
    : knob (knobToUpdate)
{
    param = state.getParameter (paramID);
    jassert (param != nullptr);

    attachment = std::make_unique<juce::ParameterAttachment> (*param,
        [this] (float) { updateKnob(); }, nullptr);

    /* OJO: setValueAsCompleteGesture toma el valor DENORMALIZADO (real),
       no el 0..1 — igual que SliderAttachment por debajo. */
    knob.onChange = [this] (double v)
    {
        attachment->setValueAsCompleteGesture ((float) v);
    };

    attachment->sendInitialUpdate();
}

KnobAttachment::~KnobAttachment()
{
    knob.onChange = nullptr;
}

void KnobAttachment::updateKnob()
{
    const auto range = param->getNormalisableRange();
    const double v = range.convertFrom0to1 (param->getValue());
    knob.setValue (v, false);
}
} // namespace floodo::brand
