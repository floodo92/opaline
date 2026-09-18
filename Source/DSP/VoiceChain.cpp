#include "VoiceChain.h"
#include <cmath>

/* ============================================================
   VOICECHAIN — cadena serial de brillo "sedoso/caro".
   Todas las profundidades se escalan por `amount` (knob Shine)
   con rampas suavizadas; los centros vienen del LearnProfile.
   Etapas: hipass → low-mid cut → de-esser → comp óptico →
   supresor resonancia → multiband(lodos) → exciter → air shelf →
   paralelo filtrado. Auto gain-match y medidores de GR en el proc.
   ============================================================ */

namespace
{
/* Histéresis para reconstruir filtros IIR solo cuando cambian de verdad. */
bool changed (float oldF, float oldG, float newF, float newG)
{
    return std::abs (oldF - newF) > 1.0f || std::abs (oldG - newG) > 0.05f;
}

/* Convierte la fundamental aprendida en un HPF conservador: siempre por
   debajo del fundamental (0.5x) para no comerse el cuerpo de voces graves
   (barítonos ~82-196 Hz) y limitado a 40-150 Hz para no dejar pasar
   sub-bajas ni recortar agudos. */
float hpFromPitch (float medianPitchHz)
{
    return juce::jlimit (40.0f, 150.0f, medianPitchHz * 0.5f);
}

/* Soft-clip final con knee suave, C0-continuo:
   - |x| <= start  -> paso exacto (ganancia 1, sin tocar material normal)
   - |x| >  start  -> curva diferencial suave hacia la asintota `maxX`.
   De esta forma un polisher nunca dispara picos sobre 0 dBFS, pero la curva
   NO corta duro (sin aliasing ni distorsión dura en el límite). */
inline float softClipSample (float x)
{
    const float start = 0.75f;    // ~ -2.5 dBFS: material normal pasa intacto
    const float maxX  = 1.0f;     // asintota (0 dBFS)
    const float ax = std::abs (x);
    if (ax <= start)
        return x;
    const float range = maxX - start;
    const float y = start + range * std::tanh ((ax - start) / range);
    return (x >= 0.0f) ? y : -y;
}

void softClipBlock (juce::AudioBuffer<float>& b)
{
    const int n = b.getNumSamples();
    const int ch = b.getNumChannels();
    for (int c = 0; c < ch; ++c)
    {
        auto* d = b.getWritePointer (c);
        for (int i = 0; i < n; ++i)
            d[i] = softClipSample (d[i]);
    }
}
}

void VoiceChain::prepare (double sr, int block, int nch)
{
    sampleRate = sr;
    numCh = nch;

    deesser.prepare    (sr, block, nch);
    optical.prepare    (sr, block, nch);
    suppressor.prepare (sr, block, nch);
    exciter.prepare    (sr, block, nch);
    eq.prepare         (sr, block, nch);
    parallel.prepare   (sr, block, nch);

    amount_s.reset (sr, 0.05);
    amount_s.setCurrentAndTargetValue (0.0f);
    bodyAir_s.reset (sr, 0.05);
    bodyAir_s.setCurrentAndTargetValue (0.0f);
}

void VoiceChain::reset()
{
    eq.reset();
    deesser.reset();
    optical.reset();
    suppressor.reset();
    exciter.reset();
    parallel.reset();
}

void VoiceChain::setAmount (float a, bool force)
{
    if (force) amount_s.setCurrentAndTargetValue (juce::jlimit (0.0f, 1.0f, a));
    else       amount_s.setTargetValue (juce::jlimit (0.0f, 1.0f, a));
}

void VoiceChain::setBodyAir (float v, bool force)
{
    if (force) bodyAir_s.setCurrentAndTargetValue (juce::jlimit (-1.0f, 1.0f, v));
    else       bodyAir_s.setTargetValue (juce::jlimit (-1.0f, 1.0f, v));
}

/* Reset de estado de cada etapa (limpia filtros y envolventes). */
void VoiceChain::OptoComp::reset()            { env.reset(); }
void VoiceChain::ResonanceSuppressor::reset()
{
    detect[0].reset(); detect[1].reset();
    res[0].reset(); res[1].reset();
    env.reset();
    grSm.setCurrentAndTargetValue (0.0f);
}
void VoiceChain::HarmonicExciter::reset()             { hp.reset(); if (os) os->reset(); }
void VoiceChain::EqStage::reset()
{
    highpass.reset(); lowMidCut.reset(); mudCut.reset(); airShelf.reset(); presence.reset();
}
void VoiceChain::setLearnProfile (const LearnProfile& p) { profile = p; }

void VoiceChain::process (juce::AudioBuffer<float>& buffer, unsigned int stageMask)
{
    const int n = buffer.getNumSamples();
    const float amount = amount_s.getNextValue();
    const float bodyAir = bodyAir_s.getNextValue();
    // getNextValue() avanza 1 muestra: hay que avanzar el resto del bloque para
    // que la rampa de 50 ms sea real (si no, dura ~25 s y el sonido "muda").
    amount_s.skip (juce::jmax (0, n - 1));
    bodyAir_s.skip (juce::jmax (0, n - 1));
    compGR = 0.0f; parGR = 0.0f;

    // 1/2/6/8: EQs (hipass, low-mid, lodos, air shelf).
    if (stageMask & kStageEq)
        eq.process (buffer, profile, amount, bodyAir);

    // 3: de-esser (solo si hay sibilancia y amount).
    if ((stageMask & kStageDeEsser) && amount > 0.001f && profile.sibilanceLevel > 0.01f)
        compGR = juce::jmax (compGR, deesser.process (buffer, amount * profile.sibilanceLevel, profile.deEssFreq, profile.sibExcessDb));

    // 4: compresor óptico.
    if ((stageMask & kStageOptical) && amount > 0.001f)
        compGR = juce::jmax (compGR, optical.process (buffer, amount));

    // 5: supresor de resonancia (el "sedoso").
    if ((stageMask & kStageSuppressor) && amount > 0.001f && profile.resonanceLevel > 0.01f)
        compGR = juce::jmax (compGR, suppressor.process (buffer, amount * profile.resonanceLevel, profile.resonanceFreq));

    // 7: exciter armónico.
    if ((stageMask & kStageExciter) && amount > 0.001f)
        exciter.process (buffer, amount * (0.4f + 0.6f * profile.brightness));

    // 9: paralelo filtrado.
    if ((stageMask & kStageParallel) && amount > 0.001f)
        parGR = parallel.process (buffer, amount);

    // Soft-clip de salida (knee suave): protege de overs cerca de 0 dBFS con
    // una curva sin distorsión dura. Solo "toca" picos > ~-6 dBFS por lo que
    // material normal pasa intacto; a Shine alto es el tope que evita que
    // drive+parallel disparen picos.
    softClipBlock (buffer);
}

/* ============ De-Esser (techo relativo al cuerpo) ============ */
/* Diseño estilo ingeniero: la "S" NUNCA debe superar el nivel del cuerpo
   500-1k (regla Paco Rincón). El techo se calcula POR MUESTRA a partir de
   la envolvente lenta del cuerpo; la banda que se resta de la señal es la
   MISMA muestra que sale (sin retardos -> sin residuos ni filtro peine),
   el detector es linked (max de canales) y cada canal tiene su propio
   estado de filtro. Sin look-ahead: attack 1 ms cubre el ataque real de
   una "S" (10-30 ms) sin añadir latencia a la cadena. */

void VoiceChain::DeEsser::prepare (double sr, int block, int nch)
{
    sampleRate = sr; this->nch = nch;
    lastCentre = -1.0f;
    gainState  = 1.0f;

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, block),
                                  (juce::uint32) juce::jmax (1, nch) };
    for (int c = 0; c < 2; ++c)
    {
        bp[c].prepare (spec);
        bp[c].coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, 7000.0f, 1.0f);
        bp[c].reset();

        // Referencia de cuerpo = banda 500-1k (HP 500 + LP 1k). Con un HP
        // solo, la sibilancia entraba en la referencia y el techo perseguía
        // a la "S" hacia arriba (pumping / under-reacción).
        bodyHP[c].prepare (spec);
        bodyHP[c].coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 500.0f, 0.707f);
        bodyHP[c].reset();
        bodyLP[c].prepare (spec);
        bodyLP[c].coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, 1000.0f, 0.707f);
        bodyLP[c].reset();
    }

    // Detector de sibilancia: attack 1 ms.
    envSib.reset (sr, 1.0f, 50.0f, false);
    // Referencia de cuerpo lenta (release 300 ms): el techo se mantiene
    // estable mientras suena la "S", no "respira" con ella.
    envBody.reset (sr, 10.0f, 300.0f, false);
}

void VoiceChain::DeEsser::reset()
{
    for (int c = 0; c < 2; ++c) { bp[c].reset(); bodyHP[c].reset(); bodyLP[c].reset(); }
    envSib.reset();
    envBody.reset();
    gainState = 1.0f;
    lastCentre = -1.0f;
}

float VoiceChain::DeEsser::process (juce::AudioBuffer<float>& b, float amount, float centre, float sibExcessDb)
{
    const int n = b.getNumSamples();
    const int ch = juce::jmin (nch, b.getNumChannels());
    if (n <= 0 || ch <= 0) return 0.0f;

    // Centro: el que calibró el Learn (deEssFreq). Histéresis: solo se
    // reconstruyen los coeficientes si cambió de verdad (evita alloc + reset
    // en cada bloque de audio).
    if (std::abs (centre - lastCentre) > 1.0f)
    {
        for (int c = 0; c < 2; ++c)
        {
            bp[c].coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sampleRate, centre, 1.0f);
            bp[c].reset();
        }
        lastCentre = centre;
    }

    // Techo dinámico (regla Paco Rincón): la "S" no debe superar el cuerpo.
    // ceilDb baja con Shine (más estricto) y con el exceso medido por el
    // Learn; con ratio finito el nivel TIENDE al techo (compresión musical,
    // no clamp duro).
    const float ceilDb   = -(0.35f + 2.2f * amount) - 0.12f * juce::jmax (0.0f, sibExcessDb);
    const float ceilingK = std::pow (10.0f, ceilDb / 20.0f);
    const float kneeDb   = 2.0f;
    const float halfKnee = kneeDb * 0.5f;
    const float ratio    = 1.0f + 3.0f * amount;
    const float slope    = 1.0f - 1.0f / ratio;      // fracción del over que se recorta
    const float relCoeff = 1.0f - std::exp (-1.0f / (0.120f * (float) sampleRate + 1e-6f)); // release ~120 ms

    float grMax = 0.0f;
    float* d[2] = { b.getWritePointer (0), nullptr };
    if (ch > 1) d[1] = b.getWritePointer (1);

    for (int i = 0; i < n; ++i)
    {
        // Banda de sibilancia POR canal (estado de filtro independiente):
        // la misma señal se usa para detectar Y para la mezcla -> gain=1
        // es paso perfecto, sin inyectar residuo de otras muestras.
        const float band0 = bp[0].processSample (d[0][i]);
        const float band1 = (ch > 1) ? bp[1].processSample (d[1][i]) : 0.0f;

        // Referencia linked de cuerpo 500-1k (max de canales).
        const float body0 = bodyLP[0].processSample (bodyHP[0].processSample (d[0][i]));
        const float body1 = (ch > 1) ? bodyLP[1].processSample (bodyHP[1].processSample (d[1][i])) : 0.0f;

        const float e   = envSib.processSample  (juce::jmax (std::abs (band0), std::abs (band1)));
        const float ref = envBody.processSample (juce::jmax (std::abs (body0), std::abs (body1)));

        // Techo = cuerpo * ceilingK. Sin voz (ref bajo el piso) o sin banda
        // audible (e bajo el piso de -66 dBFS): gain=1 exacto (idle-bypass,
        // no se toca el audio).
        float gTarget = 1.0f;
        if (ref > 1e-5f && e > 5e-4f)
        {
            const float thr = ref * ceilingK;
            const float overDb = 20.0f * std::log10 (juce::jmax (1e-4f, e / juce::jmax (1e-6f, thr)));
            if (overDb > -halfKnee)
            {
                // Knee suave 2 dB, C1-continuo con la rama de ratio encima.
                float grDb;
                if (overDb >= halfKnee)
                    grDb = (overDb - halfKnee) * slope;
                else
                {
                    const float t = overDb + halfKnee;          // 0..kneeDb
                    grDb = (t * t) / (2.0f * kneeDb) * slope;
                }
                gTarget = std::pow (10.0f, -grDb / 20.0f);
            }
        }

        // Attack inmediato (protege del overshoot), release suave 120 ms.
        // gainState persiste entre bloques: sin zipper en fronteras.
        if (gTarget < gainState) gainState  = gTarget;
        else                     gainState += relCoeff * (gTarget - gainState);

        // Mezcla seco/banda de la MISMA muestra.
        d[0][i] = d[0][i] - band0 + band0 * gainState;
        if (ch > 1)
            d[1][i] = d[1][i] - band1 + band1 * gainState;

        grMax = juce::jmax (grMax, 1.0f - gainState);
    }
    return juce::jlimit (0.0f, 1.0f, grMax);
}

/* ============ Compresor óptico ============ */

void VoiceChain::OptoComp::prepare (double sr, int block, int nch)
{
    (void) block;
    sampleRate = sr; this->nch = nch;
    env.reset (sr, 10.0f, 250.0f, false);
}

float VoiceChain::OptoComp::process (juce::AudioBuffer<float>& b, float amount)
{
    const int n = b.getNumSamples();
    const int ch = juce::jmin (nch, b.getNumChannels());

    // Compresor óptico por MUESTRA: el detector (entrada real, sin escalar por
    // amount) gobierna la ganancia en cada muestra -> sin el escalón de "un GR
    // estático por bloque" que hacía zipper en las fronteras de bloque.
    // Detector linked (max de canales): el balance estéreo no camina con el GR.
    const float thr = 0.22f + 0.14f * (1.0f - amount);
    const float ratio = 1.0f + 2.5f * amount;

    // Knee suave C1-continuo (patrón del de-esser): el over se mezcla desde
    // -knee/2 hasta +knee/2 para que la entrada a compresión no "salte".
    const float kneeDb = 6.0f;
    const float halfKnee = kneeDb * 0.5f;
    const float slope = 1.0f - 1.0f / ratio;

    const float relCoeff = 1.0f - std::exp (-1.0f / (0.100f * (float) sampleRate + 1e-6f)); // ~100 ms

    float gain = 1.0f, grMax = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float lvl0 = std::abs (b.getSample (0, i));
        const float lvl1 = (ch > 1) ? std::abs (b.getSample (1, i)) : 0.0f;
        const float e = env.processSample (juce::jmax (lvl0, lvl1));

        float gTarget = 1.0f;
        if (e > thr * std::pow (10.0f, -halfKnee / 20.0f))   // entra 3 dB antes del thr
        {
            const float overDb = 20.0f * std::log10 (juce::jmax (1e-4f, e / thr));
            float grDb;
            if (overDb >= halfKnee)
                grDb = (overDb - halfKnee) * slope;
            else
            {
                // Knee suave 6 dB, C1-continuo con la rama de ratio encima
                // (mismo patrón que el de-esser).
                const float t = overDb + halfKnee;          // 0..kneeDb
                grDb = (t * t) / (2.0f * kneeDb) * slope;
            }
            gTarget = std::pow (10.0f, -grDb / 20.0f);
        }

        if (gTarget < gain)     gain = gTarget;
        else                    gain += relCoeff * (gTarget - gain);
        for (int c = 0; c < ch; ++c)
        {
            auto* d = b.getWritePointer (c);
            d[i] *= gain;
        }
        grMax = juce::jmax (grMax, 1.0f - gain);
    }
    return (float) juce::jlimit (0.0, 1.0, (double) grMax);
}

/* ============ Supresor de resonancia ============ */

void VoiceChain::ResonanceSuppressor::prepare (double sr, int block, int nch)
{
    sampleRate = sr; this->nch = nch;
    lastCentre = -1.0f;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) block, (juce::uint32) nch };
    for (int c = 0; c < 2; ++c)
    {
        detect[c].prepare (spec); res[c].prepare (spec);
        detect[c].coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, 3400.0f, 1.5f);
        res[c].coefficients    = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, 3400.0f, 1.0f);
        detect[c].reset(); res[c].reset();
    }
    env.reset (sr, 2.0f, 150.0f, false);
    grSm.reset (sr, 0.020f);
    grSm.setCurrentAndTargetValue (0.0f);
}

float VoiceChain::ResonanceSuppressor::process (juce::AudioBuffer<float>& b, float amount, float centre)
{
    if (std::abs (centre - lastCentre) > 1.0f)
    {
        for (int c = 0; c < 2; ++c)
        {
            detect[c].coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sampleRate, centre, 1.5f);
            detect[c].reset();
            res[c].coefficients    = juce::dsp::IIR::Coefficients<float>::makeBandPass (sampleRate, centre, 1.0f);
            res[c].reset();
        }
        lastCentre = centre;
    }
    const int n = b.getNumSamples();
    const int ch = juce::jmin (nch, b.getNumChannels());

    // Detecta la dureza en la banda de resonancia (detector linked: max de canales).
    float prof = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float d0 = std::abs (detect[0].processSample (b.getSample (0, i)));
        const float d1 = (ch > 1) ? std::abs (detect[1].processSample (b.getSample (1, i))) : 0.0f;
        prof = juce::jmax (prof, env.processSample (juce::jmax (d0, d1)));
    }

    const float thr = 0.12f;
    const float over = juce::jmax (0.0f, prof - thr);
    const float grTarget = juce::jlimit (0.0f, 1.0f, over * 10.0f);

    // Smoothing ~20 ms: el GR no salta entre bloques (sin zipper).
    grSm.setTargetValue (grTarget);

    // Resta la resonancia de la señal (reduction dinámica del anillo).
    for (int c = 0; c < ch; ++c)
    {
        auto* d = b.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            const float ring = res[c].processSample (d[i]);
            d[i] -= ring * 0.4f * grSm.getNextValue();
        }
    }
    return (float) juce::jlimit (0.0, 1.0, (double) grSm.getTargetValue());
}

/* ============ Exciter armónico ============ */

void VoiceChain::HarmonicExciter::prepare (double sr, int block, int nch)
{
    sampleRate = sr; this->nch = nch;

    /* Oversampling 2× (factor 1 → 2^1). Half-band polyphase IIR: poca latencia
       y fase comprometida solo cerca de Nyquist, ideal para un tanh. */
    os = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) juce::jmax (1, nch), 1,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        false, true);
    os->initProcessing ((size_t) block);
    os->reset();

    const auto osBlock = (juce::uint32) block * 2;
    juce::dsp::ProcessSpec osSpec { sr * 2.0, osBlock, (juce::uint32) nch };
    hp.prepare (osSpec);
    hp.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr * 2.0, 3000.0f, 0.707f);
    hp.reset();
}

void VoiceChain::HarmonicExciter::process (juce::AudioBuffer<float>& b, float amount)
{
    jassert (os != nullptr);

    /* Upsample todo el bloque a 2×; la saturación ahora genera armónicos que no
       se pliegan debajo de Nyquist (el "crispy pop" sin el gris del aliasing). */
    juce::dsp::AudioBlock<const float> inBlock (b);
    auto up = os->processSamplesUp (inBlock);

    const int n = (int) up.getNumSamples();
    const int ch = juce::jmin (nch, (int) up.getNumChannels());
    // "Crispy pop": saturación en la banda de presence/sizzle (por encima de 3k).
    // DRIVE FIJO y moderado, solo el MIX escala con Shine -> más Shine siempre
    // = más brillo (curva monótona). Subido vs. original para que el exciter
    // DOMINE a la compresión que (antes) apagaba el >4k a Shine alto.
    const float drive = 3.6f;
    const float mix   = 0.85f * amount;
    const float bias  = 0.16f;                       // asimetría -> armónicos pares "musicales"
    for (int c = 0; c < ch; ++c)
    {
        auto* d = up.getChannelPointer ((size_t) c);
        for (int i = 0; i < n; ++i)
        {
            // Pre-énfasis: solo la banda crujiente > 3k va al sat (rate 2×).
            float hpOut = hp.processSample (d[i]) * drive;
            // Curva de saturación rica: tanh suave + sesgo asimétrico (pares).
            float sat = std::tanh (hpOut + bias);
            sat -= std::tanh (bias);                  // re-centra el DC exacto (0 en silencio)
            // Mezcla: seco + saturado (parallel dentro de la banda).
            d[i] = d[i] * (1.0f - mix) + sat * mix;
        }
    }
    juce::dsp::AudioBlock<float> outBlock (b);
    os->processSamplesDown (outBlock);
}

/* ============ EqStage (hipass + low-mid + lodos + air shelf) ============ */

juce::dsp::IIR::Coefficients<float>::Ptr
VoiceChain::EqStage::makePeak (double sr, float freq, float q, float gainDb)
{
    // RBJ Audio-EQ-Cookbook peak filter, calculado a mano.
    // Workaround: juce::dsp::IIR::Coefficients::makePeakFilter da coeficientes
    // degenerados en JUCE 8.0.14 (bug) que silencian la señal.
    const float A   = std::pow (10.0f, gainDb / 40.0f);          // gain linear
    const float w0  = (float) (2.0 * juce::MathConstants<double>::pi * freq / sr);
    const float cw  = std::cos (w0);
    const float sw  = std::sin (w0);
    const float alpha = sw / (2.0f * q);
    const float a0  = 1.0f + alpha / A;
    const float b0  = (1.0f + alpha * A) / a0;
    const float b1  = (-2.0f * cw) / a0;
    const float b2  = (1.0f - alpha * A) / a0;
    const float a1  = (-2.0f * cw) / a0;
    const float a2  = (1.0f - alpha / A) / a0;
    // 2nd-order ctor: (b0,b1,b2, a0,a1,a2). b's ya normalizados por a0.
    return new juce::dsp::IIR::Coefficients<float> (b0, b1, b2, 1.0f, a1, a2);
}

juce::dsp::IIR::Coefficients<float>::Ptr
VoiceChain::EqStage::makeHighShelf (double sr, float freq, float q, float gainDb)
{
    // RBJ Audio-EQ-Cookbook high-shelf, calculado a mano.
    // Workaround: juce::dsp::IIR::Coefficients::makeHighShelf ROMpe la señal con
    // gain 0 (genera b0=0 en JUCE 8.0.14, mismo bug que makePeakFilter). Al ser
    // 100% manual, gain 0 = paso perfecto y boosts/cortes correctos.
    const float A   = std::pow (10.0f, gainDb / 40.0f);
    const float w0  = (float) (2.0 * juce::MathConstants<double>::pi * freq / sr);
    const float cw  = std::cos (w0);
    const float sw  = std::sin (w0);
    const float alpha = sw / (2.0f * q);
    const float twoSqA = 2.0f * std::sqrt (A) * alpha;
    const float Asq1 = A * A - 1.0f;
    const float Asq2 = A * A + 1.0f;
    const float a0   = Asq2 + twoSqA;
    const float b0   = A * (Asq2 + Asq1 * cw + twoSqA) / a0;
    const float b1   = -2.0f * A * (Asq1 + Asq2 * cw) / a0;
    const float b2   = A * (Asq2 + Asq1 * cw - twoSqA) / a0;
    const float a1   = -2.0f * (Asq1 + Asq2 * cw) / a0;
    const float a2   = (Asq2 - twoSqA) / a0;
    return new juce::dsp::IIR::Coefficients<float> (b0, b1, b2, 1.0f, a1, a2);
}

void VoiceChain::EqStage::prepare (double sr, int block, int nch)
{
    sampleRate = sr; this->nch = nch;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) block, (juce::uint32) nch };
    highpass.prepare (spec); lowMidCut.prepare (spec);
    mudCut.prepare (spec); airShelf.prepare (spec); presence.prepare (spec);
    highpass.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 85.0f, 0.707f);
    lowMidCut.coefficients = makePeak (sr, 320.0f, 1.0f, 0.0f);
    mudCut.coefficients = makePeak (sr, 250.0f, 1.2f, 0.0f);
    airShelf.coefficients = makeHighShelf (sr, 12000.0f, 0.707f, 0.0f);
    presence.coefficients = makePeak (sr, 2500.0f, 0.9f, 0.0f);
    highpass.reset(); lowMidCut.reset(); mudCut.reset(); airShelf.reset(); presence.reset();
    // Sincroniza la caché de histéresis con los coeficientes recién creados.
    lastHighpassF = 85.0f;
    lastLowMidF = 320.0f;  lastLowMidG = 0.0f;
    lastMudF    = 250.0f;  lastMudG    = 0.0f;
    lastAirF    = 12000.0f; lastAirG   = 0.0f;
    lastPresF   = 2500.0f; lastPresG   = 0.0f;
}

void VoiceChain::EqStage::process (juce::AudioBuffer<float>& b, const LearnProfile& p,
                                   float amount, float bodyAir)
{
    const float body = juce::jlimit (0.0f, 1.0f, bodyAir * 0.5f + 0.5f);
    // "Brillo caro" = cuerpo cálido intacto + agudos calibrados a la pendiente
    // objetivo. El Learn mide presencia (2-4k) vs cuerpo (500-1k) y fija
    // `airTargetDb`; el air shelf de 12k y la presencia dan el "precio"
    // escalado por Shine. Rebalanceo: presence/air suben MÁS que antes para
    // que el brillo >4k crezca de forma monótona con Shine (antes la
    // compresión apagaba el brillo a Shine alto: curva anti-monotónica).
    const float lowMidCutDb = -3.0f * amount * body;
    const float airBoostDb  =  2.5f * amount * (1.5f - body);
    const float mudCutDb    = -3.0f * amount * p.lowMidLevel;
    // Presencia: más peso al aire medido (B-5) y base de hueco más agresiva.
    const float presenceDb  =  3.5f * amount + 0.7f * amount * p.airTargetDb * (1.5f - body);

    // Solo se reconstruye el filtro (heap Coefficients + reset de estado) cuando
    // la frecuencia o la ganancia REALES cambian más que la histéresis. Sin esto
    // se haría `new` y reset() en cada bloque de audio: alloc prohibido en el
    // hilo de audio y el reset() borra la memoria del filtro en cada frontera
    // de bloque (clics/zipper). El smoothing del parámetro ya lo da `amount`.

    // HPF adaptativo: se recalcula solo si el Learn produjo un perfil válido
    // y la frecuencia del mediana de pitch cambió más de 1 Hz.
    if (p.valid)
    {
        const float hpF = hpFromPitch (p.medianPitchHz);
        if (changed (lastHighpassF, 0.0f, hpF, 0.0f))
        {
            highpass.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, hpF, 0.707f);
            highpass.reset();
            lastHighpassF = hpF;
        }
    }

    if (changed (lastLowMidF, lastLowMidG, p.lowMidFreq, lowMidCutDb))
    {
        lowMidCut.coefficients = makePeak (sampleRate, p.lowMidFreq, 1.0f, lowMidCutDb);
        lowMidCut.reset();
        lastLowMidF = p.lowMidFreq; lastLowMidG = lowMidCutDb;
    }
    if (changed (lastMudF, lastMudG, 250.0f, mudCutDb))
    {
        mudCut.coefficients = makePeak (sampleRate, 250.0f, 1.2f, mudCutDb);
        mudCut.reset();
        lastMudF = 250.0f; lastMudG = mudCutDb;
    }
    if (changed (lastAirF, lastAirG, p.airFreq, airBoostDb))
    {
        airShelf.coefficients = makeHighShelf (sampleRate, p.airFreq, 0.707f, airBoostDb);
        airShelf.reset();
        lastAirF = p.airFreq; lastAirG = airBoostDb;
    }
    if (changed (lastPresF, lastPresG, p.presenceFreq, presenceDb))
    {
        presence.coefficients = makePeak (sampleRate, p.presenceFreq, 0.9f, presenceDb);
        presence.reset();
        lastPresF = p.presenceFreq; lastPresG = presenceDb;
    }

    const int n = b.getNumSamples();
    const int ch = juce::jmin (nch, b.getNumChannels());
    for (int c = 0; c < ch; ++c)
    {
        auto* d = b.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            d[i] = highpass.processSample (d[i]);
            d[i] = lowMidCut.processSample (d[i]);
            d[i] = mudCut.processSample (d[i]);
            d[i] = presence.processSample (d[i]);
            d[i] = airShelf.processSample (d[i]);
        }
    }
}

/* ============ Paralelo filtrado ============ */

void VoiceChain::ParallelBus::prepare (double sr, int nblock, int nch)
{
    sampleRate = sr; this->nch = nch;
    par.setSize (nch, nblock, false, false, false);
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) nblock, (juce::uint32) nch };
    hpIn.prepare (spec); lpIn.prepare (spec); hpOut.prepare (spec); lpOut.prepare (spec);
    hpIn.coefficients  = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 100.0f, 0.707f);
    lpIn.coefficients  = juce::dsp::IIR::Coefficients<float>::makeLowPass  (sr, 14000.0f, 0.707f);
    hpOut.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 200.0f, 0.707f);
    lpOut.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass  (sr, 10000.0f, 0.707f);
    hpIn.reset(); lpIn.reset(); hpOut.reset(); lpOut.reset();
    env.reset (sr, 2.0f, 200.0f, false);
    mix.reset (sr, 0.05);
    mix.setCurrentAndTargetValue (0.0f);
}

void VoiceChain::ParallelBus::reset()
{
    par.clear();
    hpIn.reset(); lpIn.reset(); hpOut.reset(); lpOut.reset();
    env.reset();
    mix.setCurrentAndTargetValue (0.0f);
}

float VoiceChain::ParallelBus::process (juce::AudioBuffer<float>& dry, float amount)
{
    const int n = dry.getNumSamples();
    const int ch = juce::jmin (nch, dry.getNumChannels());

    for (int c = 0; c < ch; ++c)
        par.copyFrom (c, 0, dry, c, 0, n);

    // Filtra entrada (100 - 14k).
    for (int c = 0; c < ch; ++c)
    {
        auto* d = par.getWritePointer (c);
        for (int i = 0; i < n; ++i)
            d[i] = hpIn.processSample (lpIn.processSample (d[i]));
    }

    // Compresión agresiva 8:1, GR por MUESTRA (sin el escalón estático por bloque).
    const float* d0 = par.getReadPointer (0);
    const float thr = 0.08f;
    const float ratio = 8.0f;
    const float relCoeff = 1.0f - std::exp (-1.0f / (0.200f * (float) sampleRate + 1e-6f)); // ~200 ms
    float gain = 1.0f;
    float grMax = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float e = env.processSample (std::abs (d0[i]));
        float gTarget = 1.0f;
        if (e > thr)
        {
            const float overDb = 20.0f * std::log10 (juce::jmax (1e-4f, e / thr));
            const float reduced = overDb / ratio;
            gTarget = std::pow (10.0f, (reduced - overDb) / 20.0f);
        }
        if (gTarget < gain)     gain = gTarget;
        else                    gain += relCoeff * (gTarget - gain);
        for (int c = 0; c < ch; ++c)
        {
            auto* d = par.getWritePointer (c);
            d[i] *= gain;
        }
        grMax = juce::jmax (grMax, 1.0f - gain);
    }

    // Filtra salida (200 - 10k).
    for (int c = 0; c < ch; ++c)
    {
        auto* d = par.getWritePointer (c);
        for (int i = 0; i < n; ++i)
            d[i] = hpOut.processSample (lpOut.processSample (d[i]));
    }

    // Mezcla bajo el seco (escala por amount). La paralela es cuerpo/saturación
    // filtrado a 10k; NO debe comerse los agudos que ponen air y exciter a amount
    // alto, por eso el mix tope es moderado.
    const float m = 0.25f * amount;
    mix.setTargetValue (m);
    for (int c = 0; c < ch; ++c)
    {
        auto* d = dry.getWritePointer (c);
        auto* p = par.getReadPointer (c);
        for (int i = 0; i < n; ++i)
        {
            const float mg = mix.getNextValue();
            d[i] = d[i] * (1.0f - mg) + p[i] * mg;
        }
    }
    return (float) juce::jlimit (0.0, 1.0, (double) grMax);
}
