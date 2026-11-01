#include "PluginEditor.h"
#include "UI/BrandFonts.h"
#include "BinaryData.h"

using namespace floodo::brand;

/* FAL mark (la F en cuadrado azul) — assets/fal_mark.svg.
   Mismo posicionamiento que SignoffNative/CascadeNative: 32×32 centrado
   verticalmente en el header, con un hairline vertical de separación. */
static juce::Drawable* falMarkDrawable()
{
    static std::unique_ptr<juce::Drawable> cached;
    if (! cached)
        /* JUCE 9: createFromSVG(XmlElement) se elimino; el parser acepta el SVG
           crudo. El asset esta compilado como BinaryData, asi que va directo.
           Mismo patron que CascadeNative/SignoffNative. */
        cached = juce::Drawable::createFromSVGString (juce::String::fromUTF8 (BinaryData::fal_mark_svg,
                                                                              BinaryData::fal_mark_svgSize));
    return cached.get();
}

/*============================================================================*/

OpalineAudioProcessorEditor::Frame::Frame (OpalineAudioProcessorEditor& owner)
    : ed (owner)
{
    setOpaque (false);

    /* Los tres knobs comparten el dial Precision. El readout y el label los
       dibuja el Frame (label ARRIBA del valor + tipografías del diseño), así
       que el Knob va sin texto propio y avisa por onValueChanged para que el
       Frame repinte. */
    auto setupKnob = [this] (Knob& k, double lo, double hi, double def)
    {
        addAndMakeVisible (k);
        k.setFinish (Knob::Finish::Precision);
        k.setShowLabel (false);
        k.setShowReadout (false);
        k.setRange (lo, hi);
        k.setDefaultValue (def);
        k.onValueChanged = [this] { repaint(); };
    };

    /* SHINE — el control que manda. 0..100. */
    setupKnob (shineKnob, 0.0, 100.0, 0.0);
    shineKnob.setTooltip ("Shine - master amount of the whole polish chain");
    shineAtt = std::make_unique<KnobAttachment> (ed.proc.apvts, "shine", shineKnob);

    /* BODY / AIR — bipolar centro 0 (la marca de cero importa). */
    setupKnob (bodyAirKnob, -100.0, 100.0, 0.0);
    bodyAirKnob.setBipolar (true);
    bodyAirKnob.setTooltip ("Body / Air - tilt between low-mid warmth and 12k air");
    bodyAirAtt = std::make_unique<KnobAttachment> (ed.proc.apvts, "bodyair", bodyAirKnob);

    /* OUTPUT */
    setupKnob (outputKnob, 0.0, 100.0, 70.0);
    outputKnob.setTooltip ("Output - final level trim");
    outputAtt = std::make_unique<KnobAttachment> (ed.proc.apvts, "output", outputKnob);

    /* Meters */
    inMeter.setCompact (true);
    outMeter.setCompact (true);
    inMeter.setTitle ("IN");
    outMeter.setTitle ("OUT");
    inMeter.setChannelLabels ("L", "R");
    outMeter.setChannelLabels ("L", "R");
    inMeter.setLevelProvider ([this] { return ed.proc.getInLevels(); });
    outMeter.setLevelProvider ([this] { return ed.proc.getOutLevels(); });
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    /* Measure */
    addAndMakeVisible (measureBtn);
    measureBtn.setComponentID ("measure");
    measureBtn.setLookAndFeel (laf.get());
    measureBtn.setButtonText ("MEASURE");
    measureBtn.setClickingTogglesState (true);
    measureBtn.onClick = [this] { ed.measurePressed(); };

    /* Bypass */
    addAndMakeVisible (bypassBtn);
    bypassBtn.setComponentID ("bypass");
    bypassBtn.setLookAndFeel (laf.get());
    bypassBtn.setButtonText ("BYPASS");
    bypassBtn.setClickingTogglesState (true);
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (ed.proc.apvts, "bypass", bypassBtn);

    /* Footer izquierda: dato real de sesión (lo llena el editor) */
    addAndMakeVisible (measureStatus);
    measureStatus.setInterceptsMouseClicks (false, false);
    measureStatus.setJustificationType (juce::Justification::centredLeft);
    measureStatus.setFont (font ({ FontId::Inter, 9.0f, 0.06f, false, true, 400 }));
    measureStatus.setColour (juce::Label::textColourId, Colour (textDim));
}

/*============================================================================*/

void OpalineAudioProcessorEditor::Frame::paint (juce::Graphics& g)
{
    /* ── Header ──
       Superficie un escalón por encima del canvas: #060708 < #0e1013 < #12151a.
       Antes el header sólo se separaba por un hairline y se leía como parte
       del fondo. */
    g.setColour (Colour (surface));
    g.fillRect (0.0f, 0.0f, (float) canvasW, 52.0f);

    /* Bisel de luz en el borde superior del header (hairline blanco 4%) */
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.fillRect (0.0f, 0.0f, (float) canvasW, 1.0f);

    g.setColour (line());
    g.fillRect (0.0f, 51.0f, (float) canvasW, 1.0f);

    /* ── Panel principal ── */
    const juce::Rectangle<float> panelBox (8.0f, 52.0f, 384.0f, 400.0f);
    g.setColour (Colour (panel));
    g.fillRoundedRectangle (panelBox, rLg);

    /* ── Marca ── */
    if (auto* mark = falMarkDrawable())
    {
        /* JUCE 9: Drawable ya no tiene setTransformToFit(); drawWithin con
           stretchToFit pinta exactamente igual (mismo rect, mismo escalado). */
        mark->drawWithin (g, juce::Rectangle<float> (32.0f, 32.0f)
                                     .withCentre ({ 32.0f, 26.0f }),
                           juce::RectanglePlacement::stretchToFit, 1.0f);
    }

    g.setColour (line());
    g.fillRect (52.0f, (52.0f - 22.0f) / 2.0f, 1.0f, 22.0f);

    g.setFont (font ({ FontId::Barlow, 18.0f, 0.14f, false, true, 600 }));
    g.setColour (Colour (textHi));
    g.drawText ("OPALINE", 62, 9, 140, 18, juce::Justification::centredLeft, false);

    /* Sheen iridiscente bajo el wordmark: el único "guarismo" de marca sobre
       texto (gradiente accent → ópalo cálido → frío → accent, de
       version_qwenf.html `.brand-name::after`, interpretado en 2px). */
    {
        const float tw = juce::jmax (1.0f, textWidth (g.getCurrentFont(), "OPALINE"));
        juce::Rectangle<float> sw (62.0f, 28.0f, tw, 1.0f);
        juce::ColourGradient sg (accent().withAlpha (0.0f), sw.getX(), 0,
                                 accent().withAlpha (0.0f), sw.getRight(), 0, false);
        sg.addColour (0.18f, opalWarm().withAlpha (0.45f));
        sg.addColour (0.50f, accent().withAlpha (0.45f));
        sg.addColour (0.82f, opalCold().withAlpha (0.45f));
        g.setGradientFill (sg);
        g.fillRect (sw);
    }

    g.setFont (font ({ FontId::Inter, 9.0f, 0.18f, false, true, 400 }));
    g.setColour (Colour (textDim));
    g.drawText ("VOCAL POLISH", 62, 30, 140, 10, juce::Justification::centredLeft, false);

    /* ── Footer (dentro del panel, recortado a sus esquinas) ── */
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addRoundedRectangle (panelBox, rLg);
        g.reduceClipRegion (clip, {});

        const float fy = panelBox.getBottom() - 29.0f;   /* 423 */
        g.setColour (Colour (inset));
        g.fillRect (panelBox.getX(), fy, panelBox.getWidth(), 28.0f);
        g.setColour (line());
        g.fillRect (panelBox.getX(), fy, panelBox.getWidth(), 1.0f);

        /* Derecha: dot de estado + estado real del MEASURE.
           Bloque de ancho FIJO para que el % que cuenta (LISTENING 43% →
           100%) no empuje a la etiqueta ni al punto. La etiqueta se ancla
           por una esquina con ancho real y el número va en fuente tabular
           ocupando su columna reservada a la derecha: nada respira. */
        const auto splitStatus = [] (const juce::String& s) -> juce::StringPairArray
        {
            juce::StringPairArray r;
            const int sp = s.lastIndexOfChar (' ');
            if (sp > 0 && s.substring (sp + 1).containsOnly ("0123456789%"))
            {
                r.set ("label", s.substring (0, sp));
                r.set ("tail",  s.substring (sp + 1));
            }
            else
            {
                r.set ("label", s);
                r.set ("tail",  {});
            }
            return r;
        };
        const auto parts = splitStatus (statusRight);

        g.setFont (font ({ FontId::Inter, 8.0f, 0.08f, false, true, 500 }));
        const float labelW = juce::jmax (1.0f, textWidth (g.getCurrentFont(), parts["label"]));

        /* Columna reservada "100%" en la fuente tabular del proyecto. */
        g.setFont (font ({ FontId::Mono, 8.0f, 0.0f, false, false, 400 }));
        const float numW = parts["tail"].isEmpty() ? 0.0f
                                                    : textWidth (g.getCurrentFont(), "100%");

        const float rx = panelBox.getRight() - 14.0f;    /* 378 */
        const float gap = 6.0f;
        const float blockW = labelW + (numW > 0.0f ? gap + numW : 0.0f);
        const float tx = rx - blockW;    /* esquina fija: la etiqueta no respira */

        g.setColour (statusColour);
        g.fillEllipse (tx - 10.0f, fy + 12.0f, 4.0f, 4.0f);

        g.setColour (Colour (textDim));
        g.setFont (font ({ FontId::Inter, 8.0f, 0.08f, false, true, 500 }));
        g.drawText (parts["label"], (int) tx, (int) fy + 9, (int) labelW, 10,
                    juce::Justification::centredLeft, false);

        if (! parts["tail"].isEmpty())
        {
            g.setFont (font ({ FontId::Mono, 8.0f, 0.0f, false, false, 400 }));
            g.drawText (parts["tail"], (int) (tx + labelW + gap), (int) fy + 9,
                        (int) numW, 10, juce::Justification::centredLeft, false);
        }
    }

    /* Borde del panel AL FINAL: el fill del footer, al llegar hasta el borde
       inferior, tapaba el hairline si éste se dibujaba antes. */
    g.setColour (lineFaint());
    g.drawRoundedRectangle (panelBox.reduced (0.5f), rLg, 1.0f);

    /* ── Texto de los knobs — label ARRIBA, valor debajo ── */
    const float heroX = 122.0f, heroW = 156.0f;
    const float bodyX = 76.0f,  outX = 252.0f, smallW = 72.0f;

    /* SHINE */
    g.setFont (font ({ FontId::Barlow, 13.0f, 0.16f, false, true, 600 }));
    g.setColour (Colour (textHi));
    g.drawText ("SHINE", (int) heroX, 236, (int) heroW, 13,
                juce::Justification::centred, false);

    g.setFont (font ({ FontId::Mono, 22.0f, -0.02f, false, false, 500 }));
    g.setColour (Colour (textHi));
    g.drawText (juce::String ((int) std::round (shineKnob.getDisplayedValue())),
                (int) heroX, 252, (int) heroW, 22,
                juce::Justification::centred, false);

    g.setFont (font ({ FontId::Mono, 11.0f, 0.0f, false, false, 400 }));
    g.setColour (Colour (textMid));
    g.drawText ("%", (int) heroX, 276, (int) heroW, 11,
                juce::Justification::centred, false);

    /* BODY / AIR — bipolar, con signo */
    const int baVal = (int) std::round (bodyAirKnob.getDisplayedValue());
    g.setFont (font ({ FontId::Barlow, 10.0f, 0.12f, false, true, 500 }));
    g.setColour (Colour (textMid));
    g.drawText ("BODY / AIR", (int) bodyX, 378, (int) smallW, 10,
                juce::Justification::centred, false);

    g.setFont (font ({ FontId::Mono, 13.0f, 0.0f, false, false, 400 }));
    g.setColour (Colour (textHi));
    g.drawText ((baVal > 0 ? juce::String ("+") : juce::String()) + juce::String (baVal),
                (int) bodyX, 390, (int) smallW, 13,
                juce::Justification::centred, false);

    /* OUTPUT */
    g.setFont (font ({ FontId::Barlow, 10.0f, 0.12f, false, true, 500 }));
    g.setColour (Colour (textMid));
    g.drawText ("OUTPUT", (int) outX, 378, (int) smallW, 10,
                juce::Justification::centred, false);

    g.setFont (font ({ FontId::Mono, 13.0f, 0.0f, false, false, 400 }));
    g.setColour (Colour (textHi));
    g.drawText (juce::String ((int) std::round (outputKnob.getDisplayedValue())),
                (int) outX, 390, (int) smallW, 13,
                juce::Justification::centred, false);
}

void OpalineAudioProcessorEditor::Frame::resized()
{
    /* Meters: panel(16,24) y panel(296,24) → canvas (24,76) y (304,76).
       El componente mide exactamente 72x104: el LedMeter posiciona su
       contenido en coordenadas locales fijas. */
    inMeter.setBounds  (24, 76, 72, 104);
    outMeter.setBounds (304, 76, 72, 104);

    /* Hero: canvas 156 (= size 140 + pad 8*2) en (122,74).
       El bounds llega hasta la línea de unidad (y287) para que arrastrar
       sobre el texto también mueva el knob. */
    shineKnob.setBounds (122, 74, 156, 216);

    /* Chicos: canvas 72 en (76,300) y (252,300) */
    bodyAirKnob.setBounds (76, 300, 72, 104);
    outputKnob.setBounds  (252, 300, 72, 104);

    /* Header: ambos botones cierran exactos contra el borde del panel (x392) */
    bypassBtn.setBounds  (262, 12, 62, 28);
    measureBtn.setBounds (330, 12, 62, 28);

    /* Footer izquierda: dato de sesión */
    measureStatus.setBounds (22, 432, 300, 10);
}

/*============================================================================*/

OpalineAudioProcessorEditor::OpalineAudioProcessorEditor (OpalineAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), frame (*this), gate (*this)
{
    addAndMakeVisible (frame);
    addAndMakeVisible (gate);     // sobre el frame en z-order

    setResizable (true, true);
    setResizeLimits (300, 320, canvasW * 2, canvasH * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) canvasW / (double) canvasH);

    setSize (canvasW, canvasH);
    startTimerHz (15);

    /* El footer muestra datos reales desde el primer frame, sin esperar al
       primer tick del timer (importante también para el snapshot headless). */
    updateFooter();

    /* Gate: con calibración persistida (recall de sesión) el panel completo
       aparece al instante — el gate SOLO aplica al estado "nunca medido". */
    updateGateVisibility();
}

OpalineAudioProcessorEditor::~OpalineAudioProcessorEditor() = default;

void OpalineAudioProcessorEditor::timerCallback()
{
    const bool measuringNow = proc.isMeasuring();

    /* Gate: revela el panel en cuanto el audio confirma el perfil; mantiene
       el botón grande en fase con el Learn y repinta el % mientras escucha. */
    updateGateVisibility();
    if (gate.isVisible())
    {
        gate.measureGateBtn.setToggleState (measuringNow, juce::dontSendNotification);
        const int reason = proc.getLastFinishReason();
        gate.skipGateBtn.setVisible (! measuringNow && ! proc.hasMeasured()
                                      && (reason == 2 || reason == 3));
        if (measuringNow)
            gate.repaint();
    }

    if (measuring && !measuringNow)
    {
        // El Learn terminó solo (capturó su objetivo): apaga el botón. El hilo
        // de audio aún debe confirmar el perfil en el siguiente bloque; se da
        // un margen de ~0,7 s para no pintar "READY" por race.
        measuring = false;
        frame.measureBtn.setToggleState (false, juce::dontSendNotification);
        debounceTicks = 10;
    }

    if (! measuringNow)
    {
        if (debounceTicks > 0)
        {
            // Espera a que el hilo de audio confirme (finish → hasMeasuredFlag).
            if (--debounceTicks == 0 || proc.hasMeasured())
            {
                debounceTicks = 0;
                pendingPosition = true;
            }
        }
        else if (pendingPosition)
        {
            positionShineForProfile();
            pendingPosition = false;
        }
    }

    updateFooter();
}

/* Footer: izquierda = dato real de la sesión de audio; derecha = estado real
   del MEASURE (dot + palabra). Nada inventado: el mock pedía "IN / OUT LINKED",
   que no existe como concepto en el plugin. */
void OpalineAudioProcessorEditor::updateFooter()
{
    const double sr = proc.getSampleRate();
    const juce::String rate = sr > 0.0 ? juce::String (sr / 1000.0, 1) + " kHz"
                                       : juce::String ("--");

    const int nch = proc.getSessionChannels();
    const juce::String chans = nch <= 0 ? juce::String ("--")
                             : nch == 1 ? juce::String ("MONO")
                             : nch == 2 ? juce::String ("STEREO")
                                        : juce::String (nch) + " CH";

    const juce::String left = rate + " "
        + juce::String::charToString ((juce::juce_wchar) 0x00B7) + " " + chans;

    if (frame.measureStatus.getText() != left)
        frame.measureStatus.setText (left, juce::dontSendNotification);

    juce::String right;
    juce::Colour col (textLo);   // reposo = punto apagado

    if (measuring)
    {
        right = "LISTENING " + juce::String ((int) (proc.getMeasureProgress() * 100.0f)) + "%";
        col = Colour (cWarn);
    }
    else if (proc.hasMeasured())
    {
        right = "CALIBRATED";
        col = Colour (cSafe);
    }
    else
    {
        // Diagnóstico del último finish(): por qué no quedó un perfil válido.
        switch (proc.getLastFinishReason())
        {
            case 2:  right = "NO SIGNAL"; col = Colour (cDanger); break;
            case 3:  right = "NO VOICE";  col = Colour (cDanger); break;
            default: right = "PRESS MEASURE TO CALIBRATE"; col = Colour (cWarn); break;
        }
    }

    if (frame.statusRight != right || frame.statusColour != col)
    {
        frame.statusRight  = right;
        frame.statusColour = col;
        frame.repaint();     // el dot + el texto los pinta el Frame
    }
}

void OpalineAudioProcessorEditor::positionShineForProfile()
{
    // Solo si hay un perfil VÁLIDO (calibró voz real). Con perfil inválido
    // (captura en silencio) getRecommendedShine() == 0 y NO se mueve el knob:
    // así el primer Learn en silencio no "pone aire de golpe" y no confunde.
    if (auto* p = proc.apvts.getParameter ("shine"))
    {
        const float rec = proc.getRecommendedShine();
        if (rec > 0.0f)
            p->setValueNotifyingHost (p->convertTo0to1 (rec));
    }
}

void OpalineAudioProcessorEditor::measurePressed()
{
    measuring = ! measuring;
    if (measuring)
        proc.beginMeasure();
    else
    {
        proc.endMeasure();
        pendingPosition = true;   // el timer colocará el Shine cuando llegue el perfil
    }

    frame.measureBtn.setToggleState (measuring, juce::dontSendNotification);
    updateFooter();
    repaint();
}

/*============================================================================*/
/* GateOverlay — pantalla de primera calibración (ver PluginEditor.h) */

OpalineAudioProcessorEditor::GateOverlay::GateOverlay (OpalineAudioProcessorEditor& owner)
    : ed (owner)
{
    setOpaque (true);

    addAndMakeVisible (measureGateBtn);
    measureGateBtn.setComponentID ("measure");
    measureGateBtn.setLookAndFeel (laf.get());
    measureGateBtn.setButtonText ("MEASURE");
    measureGateBtn.setClickingTogglesState (true);
    measureGateBtn.onClick = [this] { ed.measurePressed(); };

    addAndMakeVisible (skipGateBtn);
    skipGateBtn.setComponentID ("skip");
    skipGateBtn.setLookAndFeel (laf.get());
    skipGateBtn.setButtonText ("CONTINUE WITHOUT CALIBRATION");
    skipGateBtn.setVisible (false);   // solo tras un intento fallido (escape con dignidad)
    skipGateBtn.onClick = [this]
    {
        ed.gateDismissed = true;
        ed.updateGateVisibility();
    };
}

void OpalineAudioProcessorEditor::GateOverlay::paint (juce::Graphics& g)
{
    const int w = getWidth();
    const int h = getHeight();

    g.fillAll (Colour (bg));

    /* Marca + wordmark centrados: el gate ES el producto, no un diálogo. */
    if (auto* mark = falMarkDrawable())
    {
        /* JUCE 9: drawWithin con stretchToFit = mismo render que el viejo
           setTransformToFit + draw. */
        mark->drawWithin (g, juce::Rectangle<float> (44.0f, 44.0f)
                                      .withCentre ({ w * 0.5f, h * 0.15f }),
                           juce::RectanglePlacement::stretchToFit, 1.0f);
    }

    g.setFont (font ({ FontId::Barlow, 23.0f, 0.16f, false, true, 600 }));
    g.setColour (Colour (textHi));
    g.drawText ("OPALINE", 0, (int) (h * 0.26f), w, 26,
                juce::Justification::centred, false);

    g.setFont (font ({ FontId::Inter, 9.0f, 0.20f, false, true, 400 }));
    g.setColour (Colour (textDim));
    g.drawText ("VOCAL POLISH", 0, (int) (h * 0.26f) + 26, w, 12,
                juce::Justification::centred, false);

    /* La promesa, en inglés (el plugin pide su calibración). */
    g.setFont (font ({ FontId::Inter, 13.0f, 0.0f, false, false, 400 }));
    g.setColour (Colour (textMid));
    g.drawText ("This plugin calibrates to your voice.",
                0, (int) (h * 0.44f), w, 16, juce::Justification::centred, false);
    g.drawText ("Press MEASURE during the strongest part of the vocal.",
                0, (int) (h * 0.44f) + 20, w, 16, juce::Justification::centred, false);

    /* Estado bajo el botón: progreso vivo, diagnóstico o la garantía de 4 s. */
    const bool measuringNow = ed.proc.isMeasuring();
    const int  reason       = ed.proc.getLastFinishReason();

    if (measuringNow)
    {
        g.setFont (font ({ FontId::Mono, 11.0f, 0.0f, false, false, 400 }));
        g.setColour (Colour (cWarn));
        g.drawText ("LISTENING " + juce::String ((int) (ed.proc.getMeasureProgress() * 100.0f)) + "%",
                    0, (int) (h * 0.74f), w, 14, juce::Justification::centred, false);
    }
    else if (reason == 2 || reason == 3)
    {
        g.setFont (font ({ FontId::Inter, 10.0f, 0.06f, false, true, 500 }));
        g.setColour (Colour (cDanger));
        g.drawText (reason == 2 ? "NO SIGNAL DETECTED - CHECK THE INPUT"
                                : "NO VOICE DETECTED IN THIS MATERIAL",
                    0, (int) (h * 0.74f), w, 14, juce::Justification::centred, false);
    }
    else
    {
        g.setFont (font ({ FontId::Inter, 9.0f, 0.06f, false, true, 400 }));
        g.setColour (Colour (textLo));
        g.drawText ("~4 SECONDS OF VOCAL IS ENOUGH",
                    0, (int) (h * 0.74f), w, 12, juce::Justification::centred, false);
    }
}

void OpalineAudioProcessorEditor::GateOverlay::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    measureGateBtn.setBounds ((int) (w * 0.5f) - 95, (int) (h * 0.58f), 190, 46);
    skipGateBtn.setBounds  ((int) (w * 0.5f) - 110, (int) (h * 0.80f), 220, 26);
}

void OpalineAudioProcessorEditor::updateGateVisibility()
{
    /* El gate sólo existe mientras la instancia no tenga calibración y el
       usuario no lo haya descartado. Un recall de sesión con estado medido
       NUNCA lo muestra. */
    const bool show = ! proc.hasMeasured() && ! gateDismissed;
    gate.setVisible (show);
    frame.setVisible (! show);
}

void OpalineAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colour (bg));

    /* Única sombra del layout: box-shadow 0 8px 16px rgba(0,0,0,0.5) del panel. */
    if (currentScale > 0.0f)
    {
        const auto fb = frame.getBoundsInParent().toFloat();
        juce::DropShadow sh (Colour (0x80000000), 16, { 0, 8 });
        sh.drawForRectangle (g, fb.toNearestInt());
    }
}

void OpalineAudioProcessorEditor::resized()
{
    const auto r = getLocalBounds();
    const float s = juce::jmin ((float) r.getWidth() / (float) canvasW,
                                (float) r.getHeight() / (float) canvasH);
    currentScale = s;

    /* El gate cubre TODO el editor (layout proporcional propio, sin canvas
       fijo: responde a cualquier tamaño con el aspect lockeado). */
    gate.setBounds (r);

    frame.setSize (canvasW, canvasH);
    frame.setTransform (juce::AffineTransform().scale (s));
    frame.setTopLeftPosition ((int) std::round (((float) r.getWidth() - canvasW * s) / 2.0f),
                               (int) std::round (((float) r.getHeight() - canvasH * s) / 2.0f));
}