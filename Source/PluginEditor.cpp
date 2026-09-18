#include "PluginEditor.h"
#include "UI/BrandFonts.h"

using namespace floodo::brand;

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
    shineAtt = std::make_unique<KnobAttachment> (ed.proc.apvts, "shine", shineKnob);

    /* BODY / AIR — bipolar centro 0 (la marca de cero importa). */
    setupKnob (bodyAirKnob, -100.0, 100.0, 0.0);
    bodyAirKnob.setBipolar (true);
    bodyAirAtt = std::make_unique<KnobAttachment> (ed.proc.apvts, "bodyair", bodyAirKnob);

    /* OUTPUT */
    setupKnob (outputKnob, 0.0, 100.0, 70.0);
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
    g.setColour (line());
    g.fillRect (0.0f, 51.0f, (float) canvasW, 1.0f);

    /* ── Panel principal ── */
    const juce::Rectangle<float> panelBox (8.0f, 52.0f, 384.0f, 400.0f);
    g.setColour (Colour (panel));
    g.fillRoundedRectangle (panelBox, rLg);

    /* ── Marca ── */
    g.setFont (font ({ FontId::Barlow, 18.0f, 0.14f, false, true, 600 }));
    g.setColour (Colour (textHi));
    g.drawText ("OPALINE", 14, 9, 140, 18, juce::Justification::centredLeft, false);

    g.setFont (font ({ FontId::Inter, 9.0f, 0.18f, false, true, 400 }));
    g.setColour (Colour (textDim));
    g.drawText ("VOCAL POLISH", 14, 30, 140, 10, juce::Justification::centredLeft, false);

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

        /* Derecha: dot de estado + estado real del MEASURE */
        g.setFont (font ({ FontId::Inter, 8.0f, 0.08f, false, true, 500 }));
        const float rx = panelBox.getRight() - 14.0f;    /* 378 */
        const float tw = juce::jmax (1.0f, textWidth (g.getCurrentFont(), statusRight));
        const float tx = rx - tw;

        g.setColour (statusColour);
        g.fillEllipse (tx - 10.0f, fy + 12.0f, 4.0f, 4.0f);
        g.setColour (Colour (textDim));
        g.drawText (statusRight, (int) tx, (int) fy + 10, (int) tw, 10,
                    juce::Justification::centredLeft, false);
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
    g.drawText (juce::String ((int) std::round (shineKnob.getValue())),
                (int) heroX, 252, (int) heroW, 22,
                juce::Justification::centred, false);

    g.setFont (font ({ FontId::Mono, 11.0f, 0.0f, false, false, 400 }));
    g.setColour (Colour (textMid));
    g.drawText ("%", (int) heroX, 276, (int) heroW, 11,
                juce::Justification::centred, false);

    /* BODY / AIR — bipolar, con signo */
    const int baVal = (int) std::round (bodyAirKnob.getValue());
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
    g.drawText (juce::String ((int) std::round (outputKnob.getValue())),
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
    measureStatus.setBounds (22, 433, 300, 10);
}

/*============================================================================*/

OpalineAudioProcessorEditor::OpalineAudioProcessorEditor (OpalineAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), frame (*this)
{
    addAndMakeVisible (frame);

    setResizable (true, true);
    setResizeLimits (300, 320, canvasW * 2, canvasH * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) canvasW / (double) canvasH);

    setSize (canvasW, canvasH);
    startTimerHz (15);

    /* El footer muestra datos reales desde el primer frame, sin esperar al
       primer tick del timer (importante también para el snapshot headless). */
    updateFooter();
}

OpalineAudioProcessorEditor::~OpalineAudioProcessorEditor() = default;

void OpalineAudioProcessorEditor::timerCallback()
{
    const bool measuringNow = proc.isMeasuring();

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

    const int nch = juce::jmax (proc.getTotalNumInputChannels(),
                                proc.getTotalNumOutputChannels());
    const juce::String chans = nch <= 1 ? juce::String ("MONO")
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
            default: right = "READY";     col = Colour (textLo);  break;
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

    frame.setSize (canvasW, canvasH);
    frame.setTransform (juce::AffineTransform().scale (s));
    frame.setTopLeftPosition ((int) std::round (((float) r.getWidth() - canvasW * s) / 2.0f),
                              (int) std::round (((float) r.getHeight() - canvasH * s) / 2.0f));
}