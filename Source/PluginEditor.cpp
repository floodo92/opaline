#include "PluginEditor.h"
#include "UI/BrandFonts.h"

using namespace floodo::brand;

static constexpr int HEADER_H = 44;

/*============================================================================*/

OpalineAudioProcessorEditor::Frame::Frame (OpalineAudioProcessorEditor& owner)
    : ed (owner)
{
    setOpaque (false);

    /* SHINE — el único control que manda. 0..100. */
    addAndMakeVisible (shineKnob);
    shineKnob.setLabelText ("Shine");
    shineKnob.setShowLabel (true);
    shineKnob.setRange (0.0, 100.0);
    shineKnob.setDefaultValue (0.0);
    shineKnob.setFormatValue ([] (double v) { return juce::String ((int) v) + "%"; });
    shineAtt = std::make_unique<KnobAttachment> (ed.proc.apvts, "shine", shineKnob);

    /* BODY / AIR — bipolar centro 0. */
    addAndMakeVisible (bodyAirKnob);
    bodyAirKnob.setLabelText ("Body / Air");
    bodyAirKnob.setShowLabel (true);
    bodyAirKnob.setRange (-100.0, 100.0);
    bodyAirKnob.setDefaultValue (0.0);
    bodyAirKnob.setBipolar (true);
    bodyAirAtt = std::make_unique<KnobAttachment> (ed.proc.apvts, "bodyair", bodyAirKnob);

    /* OUTPUT */
    addAndMakeVisible (outputKnob);
    outputKnob.setLabelText ("Output");
    outputKnob.setShowLabel (true);
    outputKnob.setRange (0.0, 100.0);
    outputKnob.setDefaultValue (70.0);
    outputAtt = std::make_unique<KnobAttachment> (ed.proc.apvts, "output", outputKnob);

    /* Meters */
    inMeter.setChannelLabels ("IN L", "IN R");
    inMeter.setLevelProvider ([this] { return ed.proc.getInLevels(); });
    outMeter.setChannelLabels ("OUT L", "OUT R");
    outMeter.setLevelProvider ([this] { return ed.proc.getOutLevels(); });
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    /* Measure (pill con look de marca) */
    addAndMakeVisible (measureBtn);
    measureBtn.setComponentID ("measure");
    measureBtn.setLookAndFeel (laf.get());
    measureBtn.setButtonText ("MEASURE");
    measureBtn.setClickingTogglesState (true);
    measureBtn.onClick = [this] { ed.measurePressed(); };

    /* Bypass (toggle pill, verde cuando activo) */
    addAndMakeVisible (bypassBtn);
    bypassBtn.setComponentID ("bypass");
    bypassBtn.setLookAndFeel (laf.get());
    bypassBtn.setButtonText ("BYPASS");
    bypassBtn.setClickingTogglesState (true);
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (ed.proc.apvts, "bypass", bypassBtn);

    addAndMakeVisible (measureStatus);
    measureStatus.setJustificationType (juce::Justification::centred);
    measureStatus.setText ("not measured", juce::dontSendNotification);
}

/*============================================================================*/

void OpalineAudioProcessorEditor::Frame::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    /* .plugin-frame: panel + borde hairline-strong + radius lg */
    g.setColour (Colour (panel));
    g.fillRoundedRectangle (b, rLg);
    g.setColour (lineStrong());
    g.drawRoundedRectangle (b.reduced (0.5f), rLg, 1.0f);

    /* Header hairline */
    g.setColour (line());
    g.fillRect (0, HEADER_H - 1, getWidth(), 1);

    /* Header content (coords de diseño 400×460, ya escaladas por el transform) */
    const auto hb = getLocalBounds().withHeight (HEADER_H).reduced (24, 0);
    g.setFont (font (tDisplaySM()));
    g.setColour (accentAlpha (0.75f));
    g.drawText ("VOCAL POLISH", hb, juce::Justification::bottomLeft, false);

    g.setFont (font ({ FontId::Barlow, 22.0f, 0.16f, true, true }));
    g.setColour (Colour (textHi));
    g.drawText ("OPALINE", hb, juce::Justification::topLeft, false);
}

void OpalineAudioProcessorEditor::Frame::resized()
{
    const int W = getWidth();
    const int H = getHeight();
    const int bodyY = HEADER_H;

    /* ── header pills (derecha) ── */
    const int pillH = 26;
    const int mW = 92, bW = 84;
    const int pillY = (HEADER_H - pillH) / 2 - 3;
    measureBtn.setBounds (W - 24 - mW, pillY, mW, pillH);
    bypassBtn.setBounds (W - 24 - mW - 8 - bW, pillY, bW, pillH);

    /* ── Meters: IN | OUT lado a lado ── */
    const int mY = bodyY + 8;
    const int mH = 52;
    const int rowW = W - 24 * 2;
    const int gap = 12;
    const int mMeterW = (rowW - gap) / 2;
    inMeter.setBounds (24, mY, mMeterW, mH);
    outMeter.setBounds (24 + mMeterW + gap, mY, mMeterW, mH);

    /* ── Hero: SHINE (el "one knob") ── */
    const float shineD = shineKnob.diameter();
    const int heroY = mY + mH + 8;
    shineKnob.setBounds ((int) ((W - shineD) / 2.0f), heroY,
                         (int) shineD, (int) shineKnob.totalHeight());

    /* ── Knobs secundarios: Body/Air + Output ── */
    const float kD = bodyAirKnob.diameter();
    const int kY = heroY + (int) shineKnob.totalHeight() + 8;
    const float totW = kD + 16.0f + outputKnob.diameter();
    const int kX = (int) ((W - totW) / 2.0f);
    bodyAirKnob.setBounds (kX, kY, (int) kD, (int) bodyAirKnob.totalHeight());
    outputKnob.setBounds (kX + (int) (kD + 16.0f), kY,
                          (int) outputKnob.diameter(), (int) outputKnob.totalHeight());

    /* ── Status ── */
    measureStatus.setBounds (0, kY + (int) bodyAirKnob.totalHeight() + 6,
                             W, 16);
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
}

OpalineAudioProcessorEditor::~OpalineAudioProcessorEditor() = default;

void OpalineAudioProcessorEditor::timerCallback()
{
    const bool measuringNow = proc.isMeasuring();

    if (measuring && !measuringNow)
    {
        // El Learn terminó solo (capturó su objetivo): apaga el botón. El hilo
        // de audio aún debe confirmar el perfil en el siguiente bloque; se da
        // un margen de ~0,7 s para no pintar "not measured" por race.
        measuring = false;
        frame.measureBtn.setButtonText ("MEASURE");
        frame.measureBtn.setToggleState (false, juce::dontSendNotification);
        debounceTicks = 10;
    }

    if (measuringNow)
    {
        frame.measureStatus.setText ("listening… " + juce::String ((int) (proc.getMeasureProgress() * 100.0f)) + "%",
                                     juce::dontSendNotification);
    }
    else if (debounceTicks > 0)
    {
        // Espera a que el hilo de audio confirme (finish → hasMeasuredFlag).
        if (--debounceTicks == 0 || proc.hasMeasured())
        {
            debounceTicks = 0;
            updateMeasureStatus();
            pendingPosition = true;
        }
    }
    else if (pendingPosition)
    {
        positionShineForProfile();
        pendingPosition = false;
    }
}

void OpalineAudioProcessorEditor::updateMeasureStatus()
{
    if (proc.hasMeasured())
    {
        frame.measureStatus.setText ("voice calibrated", juce::dontSendNotification);
        return;
    }

    // Diagnóstico: motivo de invalidez del último finish().
    const int r = proc.getLastFinishReason();
    if (r == 2)
        frame.measureStatus.setText ("not measured — no signal (is audio reaching the track?)",
                                     juce::dontSendNotification);
    else if (r == 3)
        frame.measureStatus.setText ("not measured — no voice content",
                                     juce::dontSendNotification);
    else
        frame.measureStatus.setText ("not measured",
                                     juce::dontSendNotification);
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

    frame.measureBtn.setButtonText (measuring ? "CAPTURING…" : "MEASURE");
    frame.measureBtn.setToggleState (measuring, juce::dontSendNotification);
    frame.measureStatus.setText (measuring ? "listen — analyzing your voice"
                                           : (proc.hasMeasured() ? "voice calibrated" : "not measured"),
                                 juce::dontSendNotification);
    repaint();
}

void OpalineAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colour (bg));

    if (currentScale > 0.0f)
    {
        const auto fb = frame.getBoundsInParent().toFloat();
        juce::DropShadow sh (Colour (0x8c000000), 24, { 0, 8 });
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