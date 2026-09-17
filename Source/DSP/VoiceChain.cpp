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
void VoiceChain::DeEsser::reset()             { bp.reset(); env.reset(); }
void VoiceChain::OptoComp::reset()            { env.reset(); }
void VoiceChain::ResonanceSuppressor::reset() { detect.reset(); res.reset(); env.reset(); }
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
}

/* ============ De-Esser ============ */

void VoiceChain::DeEsser::prepare (double sr, int block, int nch)
{
    sampleRate = sr; this->nch = nch;
    lastCentre = -1.0f;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) block, (juce::uint32) nch };
    bp.prepare (spec);
    bp.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, 7000.0f, 1.0f);
    bp.reset();
    env.reset (sr, 1.0f, 50.0f, false);
}

float VoiceChain::DeEsser::process (juce::AudioBuffer<float>& b, float amount, float centre, float sibExcessDb)
{
    // Solo se reconstruye el filtro si cambió el centro (tras un Learn, raro);
    // evita heap-alloc de Coefficients y reset() en cada bloque de audio.
    if (std::abs (centre - lastCentre) > 1.0f)
    {
        bp.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sampleRate, centre, 1.0f);
        bp.reset();
        lastCentre = centre;
    }
    const int n = b.getNumSamples();
    const int ch = juce::jmin (nch, b.getNumChannels());

    // De-esser dinámico por banda (idle-bypass): el detector es la envolvente de
    // la propia banda; solo cuando la "S" supera el umbral baja la ganancia de la
    // banda. En reposo, gain=1 -> la señal pasa intacta (se acabó el fuzz constante
    // y el "+3x" que sumaba brillo en toda la banda 5-10k).
    // El umbral baja con el exceso medido por el Learn (sibExcessDb) y con Shine.
    // Mapeo suave: en el corpus las "S" de estudio muestran exceso 15-24 dB frente
    // al cuerpo; 0.88^(24/6)=0.60 -> el de-esser SOLO profundiza, nunca aísla la banda.
    const float thr1  = 0.55f - 0.40f * amount;
    const float thr   = thr1 * std::pow (0.88f, juce::jmax (0.0f, sibExcessDb) / 6.0f);
    const float ratio = 1.0f + 2.5f * amount;
    const float relCoeff = 1.0f - std::exp (-1.0f / (0.120f * (float) sampleRate + 1e-6f)); // release ~120 ms

    float gain = 1.0f, grMax = 0.0f;
    for (int c = 0; c < ch; ++c)
    {
        auto* dd = b.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            const float band = bp.processSample (dd[i]);
            const float e = (c == 0) ? env.processSample (band) : env.getCurrentValue();
            // Target de ganancia desde el nivel de la banda (soft knee por encima
            // del umbral); attack inmediato, release suave a one-pole.
            float gTarget = 1.0f;
            if (e > thr)
            {
                const float overDb = 20.0f * std::log10 (juce::jmax (1e-4f, e / thr));
                gTarget = std::pow (10.0f, (-ratio * overDb) / 20.0f);
            }
            if (gTarget < gain)     gain = gTarget;
            else                    gain += relCoeff * (gTarget - gain);
            // Mezcla de banda: seco - banda + banda*gain (gain=1 -> bypass perfecto).
            dd[i] = dd[i] - band + band * gain;
        }
        grMax = juce::jmax (grMax, 1.0f - gain);
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
    const float* d0 = b.getReadPointer (0);

    // Compresor óptico por MUESTRA: el detector (entrada real, sin escalar por
    // amount) gobierna la ganancia en cada muestra -> sin el escalón de "un GR
    // estático por bloque" que hacía zipper en las fronteras de bloque.
    const float thr = 0.18f + 0.18f * (1.0f - amount);
    const float ratio = 1.0f + 3.5f * amount;
    const float relCoeff = 1.0f - std::exp (-1.0f / (0.100f * (float) sampleRate + 1e-6f)); // ~100 ms

    float gain = 1.0f, grMax = 0.0f;
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
    detect.prepare (spec); res.prepare (spec);
    detect.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, 3400.0f, 1.5f);
    res.coefficients    = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, 3400.0f, 1.0f);
    detect.reset(); res.reset();
    env.reset (sr, 2.0f, 150.0f, false);
}

float VoiceChain::ResonanceSuppressor::process (juce::AudioBuffer<float>& b, float amount, float centre)
{
    if (std::abs (centre - lastCentre) > 1.0f)
    {
        detect.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sampleRate, centre, 1.5f);
        detect.reset();
        res.coefficients    = juce::dsp::IIR::Coefficients<float>::makeBandPass (sampleRate, centre, 1.0f);
        res.reset();
        lastCentre = centre;
    }
    const int n = b.getNumSamples();
    const int ch = juce::jmin (nch, b.getNumChannels());

    // Detecta la dureza en la banda de resonancia.
    const float* d0 = b.getReadPointer (0);
    float prof = 0.0f;
    for (int i = 0; i < n; ++i)
        prof = juce::jmax (prof, env.processSample (std::abs (detect.processSample (d0[i]))));

    const float thr = 0.12f;
    const float over = juce::jmax (0.0f, prof - thr);
    const float gr = juce::jlimit (0.0f, 1.0f, over * 10.0f);

    // Resta la resonancia de la señal (reduction dinámica del anillo).
    for (int c = 0; c < ch; ++c)
    {
        auto* d = b.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            const float ring = res.processSample (d[i]);
            d[i] -= ring * 0.4f * gr;
        }
    }
    return gr;
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
    // DRIVE FIJO y moderado (evita que a amount alto la saturación aplaste los
    // agudos); solo el MIX escala con Shine -> más Shine siempre = más brillo.
    const float drive = 3.2f;
    const float mix   = 0.55f * amount;
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
    // `airTargetDb`; el air shelf de 12k da el "precio" fijo escalado por Shine
    // (ya no dispara +15 dB porque la medida dejó de estar clavada en el clamp).
    const float lowMidCutDb = -3.0f * amount * body;
    const float airBoostDb  =  0.6f * amount * (1.5f - body);
    const float mudCutDb    = -3.0f * amount * p.lowMidLevel;
    // Presencia (2.5k): la regla del triángulo empuja los agudos hacia B-5 usando
    // el airTargetDb medido sobre presencia vs cuerpo; el knob air/body modula.
    const float presenceDb  =  2.0f * amount + 0.5f * amount * p.airTargetDb * (1.5f - body);

    // Solo se reconstruye el filtro (heap Coefficients + reset de estado) cuando
    // la frecuencia o la ganancia REALES cambian más que la histéresis. Sin esto
    // se haría `new` y reset() en cada bloque de audio: alloc prohibido en el
    // hilo de audio y el reset() borra la memoria del filtro en cada frontera
    // de bloque (clics/zipper). El smoothing del parámetro ya lo da `amount`.
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
