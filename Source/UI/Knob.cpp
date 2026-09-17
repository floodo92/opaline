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
    const float cw = knobDiameter();
    const float ch = knobDiameter();
    const float cx = cw / 2.0f;
    const float cy = ch / 2.0f;

    const double norm       = clampNorm ((value - min) / (max - min));
    const double valueAngle = normToAngle (norm);
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

    /* ── Cara del dial — grafito abombado (radial desde arriba-izq) ── */
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

        g.setColour (active ? Colour (0xffffffff) : Colour (0xebf0f0f4));
        g.fillPath (blade);
        juce::PathStrokeType st (0.7f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt);
        g.setColour (Colour (0x59000000));
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

    /* ── Readout (.knob-val) + label (.knob-lbl) debajo ── */
    const bool revealReadout = ! (readoutReveal && ! active);
    if (revealReadout)
    {
        g.setFont (font (tMono()));
        g.setColour (active ? accent() : Colour (textHi));
        const int valY = (int) ch + rowGap;
        g.drawText (displayText(), 0, valY, (int) cw, readoutH,
                    juce::Justification::centredTop, false);
    }
    if (showLabel && labelText.isNotEmpty())
    {
        g.setFont (font (tDisplayMD()));
        g.setColour (active ? juce::Colours::white.withAlpha (0.5f) : Colour (textLo));
        const int lblY = (int) ch + rowGap + readoutH;
        g.drawText (labelText, 0, lblY, (int) cw, labelH,
                    juce::Justification::centredTop, false);
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
