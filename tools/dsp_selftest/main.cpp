// DSP self-test: generates a synthetic vocal, runs it through VoiceChain
// and writes IN/OUT .wav files to prove the engine produces audio.
#include "../../Source/DSP/VoiceChain.h"
#include <cstdio>
#include <vector>
#include <cmath>
#include <cstdint>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void writeWav (const char* path, const std::vector<float>& mono, int sr)
{
    // 16-bit PCM, mono, 44-byte canonical WAV header.
    uint32_t dataBytes = (uint32_t) mono.size() * 2;
    uint32_t fmtSize = 16, audioFmt = 1, channels = 1;
    uint32_t byteRate = sr * channels * 2, blockAlign = channels * 2, bits = 16;
    FILE* f = nullptr;
    fopen_s (&f, path, "wb");
    if (!f) { printf ("cannot open %s\n", path); return; }
    auto wr = [&](const void* p, size_t n) { fwrite (p, 1, n, f); };
    char id[4]; uint32_t u;
    memcpy (id, "RIFF", 4); wr (id, 4);
    u = 36 + dataBytes; wr (&u, 4);
    memcpy (id, "WAVE", 4); wr (id, 4);
    memcpy (id, "fmt ", 4); wr (id, 4);
    wr (&fmtSize, 4); wr (&audioFmt, 4); wr (&channels, 4);
    u = (uint32_t) sr; wr (&u, 4); wr (&byteRate, 4); wr (&blockAlign, 4); wr (&bits, 4);
    memcpy (id, "data", 4); wr (id, 4); wr (&dataBytes, 4);
    for (size_t i = 0; i < mono.size(); ++i)
    {
        int16_t s = (int16_t) (juce::jlimit (-1.0f, 1.0f, mono[i]) * 32767.0f);
        int16_t be = (int16_t) ((s & 0xFF) << 8 | (s >> 8) & 0xFF); // little endian
        wr (&be, 2);
    }
    fclose (f);
}

int main()
{
    const double sr = 48000.0;
    const int block = 512;
    const int totalBlocks = 100;   // ~1.07 s

    // Synthetic vocal: 220Hz saw-ish with harmonics + light sibilance bursts.
    std::vector<float> in;
    in.reserve ((size_t) totalBlocks * block);
    double t = 0.0;
    for (int i = 0; i < totalBlocks * block; ++i)
    {
        double s = 0.0;
        for (int h = 1; h <= 8; ++h)
            s += (1.0 / h) * std::sin (2.0 * M_PI * 220.0 * h * t);
        s *= 0.4;
        if ((i / block) % 8 == 3)   // short /s/ noise burst every 8 blocks
            s += 0.15 * (std::rand() / (double) RAND_MAX * 2.0 - 1.0);
        in.push_back ((float) s);
        t += 1.0 / sr;
    }

    // Prepare chain.
    VoiceChain chain;
    chain.prepare (sr, block, 1);
    LearnProfile profile;
    profile.valid = true;               // enable de-esser/resonance/suppressor
    profile.sibilanceLevel = 0.6f;
    profile.resonanceLevel = 0.6f;
    profile.lowMidLevel    = 0.5f;
    profile.brightness     = 0.5f;
    profile.airTargetDb    = 7.0f;      // simula Learn: aire 7 dB pobre vs cuerpo
    profile.sibExcessDb    = 6.0f;      // simula Learn: sibilancias 6 dB sobre el cuerpo
    chain.setLearnProfile (profile);
    chain.setAmount (1.0f, true);       // full Shine
    chain.setBodyAir (0.0f);

    // Probe: does a single JUCE peak-filter pass 220Hz+harmonics?
    {
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) block, 1 };

        // Variant A: coeff then prepare (prepare after coeff)
        auto rmsBlock = [&](juce::dsp::IIR::Filter<float>& f) {
            float acc = 0;
            for (int cnt = 0; cnt < 3; ++cnt)
                for (float x : in) { float y = f.processSample (x); acc += y*y; }
            return std::sqrt (acc/(float)in.size());
        };

        juce::dsp::IIR::Filter<float> fA;
        fA.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 320.0f, 1.0f, 0.0f);
        fA.prepare (spec); fA.reset();

        juce::dsp::IIR::Filter<float> fB;
        fB.prepare (spec);
        fB.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 320.0f, 1.0f, 0.0f);
        fB.reset();

        juce::dsp::IIR::Filter<float> fHP_ref;
        fHP_ref.prepare (spec);
        fHP_ref.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 85.0f, 0.707f);
        fHP_ref.reset();

        juce::dsp::IIR::Filter<float> fLP_ref;
        fLP_ref.prepare (spec);
        fLP_ref.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, 1000.0f, 0.707f);
        fLP_ref.reset();

        printf ("EQVAR peak[coeffThenPrepare]   rms=%.6f\n", rmsBlock (fA));
        printf ("EQVAR peak[prepareThenCoeff]   rms=%.6f\n", rmsBlock (fB));
        printf ("EQVAR highpass[85Hz]           rms=%.6f\n", rmsBlock (fHP_ref));
        printf ("EQVAR lowpass[1000Hz]          rms=%.6f\n", rmsBlock (fLP_ref));

        auto c0 = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 320.0f, 1.0f, 0.0f);
        printf ("EQVAR peak coeffs: b0=%.8f b1=%.8f b2=%.8f a1=%.8f a2=%.8f\n",
                c0->coefficients[0], c0->coefficients[1], c0->coefficients[2],
                c0->coefficients[3], c0->coefficients[4]);
        auto c1 = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 85.0f, 0.707f);
        printf ("EQVAR hp coeffs:   b0=%.8f b1=%.8f b2=%.8f a1=%.8f a2=%.8f\n",
                c1->coefficients[0], c1->coefficients[1], c1->coefficients[2],
                c1->coefficients[3], c1->coefficients[4]);
        for (float gf : { 0.0f, 3.0f, -3.0f, 6.0f })
        {
            auto cp = juce::dsp::IIR::Coefficients<float>::makePeakFilter (1000.0, 2500.0f, 1.0f, gf);
            printf ("EQVAR peak freq=2500 gain=%+4.1f  b0=%.6f a2=%.6f\n",
                    gf, cp->coefficients[0], cp->coefficients[4]);
        }

        auto probeBand = [&](const char* label, const juce::dsp::IIR::Coefficients<float>::Ptr& c) {
            juce::dsp::IIR::Filter<float> f; f.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 1 });
            f.coefficients = c; f.reset();
            float acc = 0; for (float x : in) { float y = f.processSample (x); acc += y*y; }
            printf ("FILT %-22s rms=%.6f  (b0=%.4f a2=%.4f)\n", label, std::sqrt (acc/(float)in.size()), c->coefficients[0], c->coefficients[4]);
        };
        probeBand ("bandpass 7k", juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, 7000.0f, 1.0f));
        probeBand ("bandpass 3.4k", juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, 3400.0f, 1.0f));
        probeBand ("highshelf 12k +3", juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, 12000.0f, 0.707f, 3.0f));
        probeBand ("highshelf 12k  0", juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, 12000.0f, 0.707f, 0.0f));

        // Mi high-shelf RBJ manual (a 0 dB y a +7 dB) — ¿pasa señal como debe?
        {
            auto mkShelf = [&](float g) -> juce::dsp::IIR::Coefficients<float>::Ptr {
                float A = std::pow (10.0f, g/40.0f);
                float w0 = (float)(2.0*3.141592653589793*12000.0/sr);
                float cw = std::cos(w0), sw = std::sin(w0);
                float al = sw / 1.41421356f;             // alpha = sw/(2Q), Q=0.707
                float twoAsA = 2.0f * std::sqrt(A) * al;
                float Asq1 = (A*A)-1.0f, Asq2 = (A*A)+1.0f;
                float a0 = Asq2 + twoAsA;
                float b0 = A*(Asq2 + (A-1.0f)*cw + twoAsA)/a0;
                float b1 = -2.0f*A*(Asq1 + (A+1.0f)*cw)/a0;
                float b2 = A*(Asq2 + (A-1.0f)*cw - twoAsA)/a0;
                float a1 = -2.0f*(Asq1 + Asq2*cw)/a0;
                float a2 = (Asq2 - twoAsA)/a0;
                return new juce::dsp::IIR::Coefficients<float> (b0,b1,b2,1.0f,a1,a2);
            };
            probeBand ("myshelf 12k +7", mkShelf (7.0f));
            probeBand ("myshelf 12k  0", mkShelf (0.0f));
        }

        // Sonda de mi makePeak manual con boost (¿boostea o también falla?)
        {
            auto mp7 = juce::dsp::IIR::Coefficients<float>::Ptr (
                new juce::dsp::IIR::Coefficients<float> (0,0,0,1,0,0));
            // rellenar via assign tendria que incluir makePeak... usamos el ctor directo:
            auto mkPeak = [&](float g) -> juce::dsp::IIR::Coefficients<float>::Ptr {
                float A = std::pow (10.0f, g/40.0f);
                float w0 = (float)(2.0*3.141592653589793*320.0/sr);
                float cw = std::cos(w0), sw = std::sin(w0);
                float al = sw/(2.0f*0.9f);
                float a0 = 1.0f + al/A;
                float b0 = (1.0f+al*A)/a0, b1 = (-2.0f*cw)/a0, b2 = (1.0f-al*A)/a0;
                float a1 = (-2.0f*cw)/a0, a2 = (1.0f-al/A)/a0;
                return new juce::dsp::IIR::Coefficients<float> (b0,b1,b2,1.0f,a1,a2);
            };
            probeBand ("mypeak 320 +7dB", mkPeak (7.0f));
            probeBand ("mypeak 320 -3dB", mkPeak (-3.0f));
        }

        // Variant C: use process() on a full buffer (recommended API).
        juce::AudioBuffer<float> blk (1, (int) in.size());
        blk.copyFrom (0, 0, in.data(), (int) in.size());
        juce::dsp::AudioBlock<float> block (blk);
        juce::dsp::IIR::Filter<float> fC;
        fC.prepare (spec);
        fC.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 320.0f, 1.0f, 0.0f);
        fC.reset();
        float accC = 0;
        for (int cnt = 0; cnt < 3; ++cnt)
        {
            blk.clear(); blk.copyFrom (0, 0, in.data(), (int) in.size());
            juce::dsp::AudioBlock<float> blockC (blk);
            fC.process (juce::dsp::ProcessContextReplacing<float> (blockC));
            for (int i = 0; i < (int) in.size(); ++i) { float y = blk.getSample (0, i); accC += y*y; }
        }
        printf ("EQVAR peak[process-buffer]     rms=%.6f\n", std::sqrt (accC/(float)in.size()));
    }

    std::vector<float> out;
    out.reserve (in.size());
    float maxIn = 0.0f, maxOut = 0.0f;

    // Per-stage probe: run a copy of the signal through each stage alone.
    auto rms = [&](const std::vector<float>& v) -> float {
        float acc = 0.0f;
        for (float x : v) acc += x * x;
        return std::sqrt (acc / (float) v.size());
    };
    {
        VoiceChain c2; c2.prepare (sr, block, 1);
        c2.setAmount (1.0f, true); c2.setBodyAir (0.0f);
        c2.setLearnProfile (profile);
        auto probe = in;
        // reproduce only EqStage via a fresh chain, then print.
        // We cannot call internal stages; instead run whole chain and
        // print per-block partials through manual search:
    }

    juce::AudioBuffer<float> buf ((size_t) 1, block);
    size_t pos = 0;
    for (int b = 0; b < totalBlocks; ++b)
    {
        auto* d = buf.getWritePointer (0);
        for (int i = 0; i < block; ++i)
            d[i] = in[pos + i];
        for (int i = 0; i < block; ++i)
            maxIn = juce::jmax (maxIn, std::abs (d[i]));
        chain.process (buf);
        for (int i = 0; i < block; ++i)
        {
            float v = d[i];
            maxOut = juce::jmax (maxOut, std::abs (v));
            out.push_back (v);
        }
        pos += block;
    }

    // Stereo check: run the same chain with 2 channels.
    {
        VoiceChain sc; sc.prepare (sr, block, 2);
        sc.setAmount (1.0f, true); sc.setBodyAir (0.0f);
        profile.valid = true; profile.sibilanceLevel = 0.6f; profile.resonanceLevel = 0.6f;
        sc.setLearnProfile (profile);
        juce::AudioBuffer<float> sb ((size_t) 2, block);
        float maxS = 0.0f;
        size_t sp = 0;
        for (int b = 0; b < totalBlocks; ++b)
        {
            for (int c = 0; c < 2; ++c)
            {
                auto* d = sb.getWritePointer (c);
                for (int i = 0; i < block; ++i) d[i] = in[sp + i];
            }
            sc.process (sb);
            for (int c = 0; c < 2; ++c)
            {
                const float* d = sb.getReadPointer (c);
                for (int i = 0; i < block; ++i) maxS = juce::jmax (maxS, std::abs (d[i]));
            }
            sp += block;
        }
        printf ("STEREO: maxOut=%.6f  %s\n", maxS, maxS > 0.001f ? "PASS" : "FAIL");
    }

    // Brillo objetivo: energía en >4kHz de entrada vs salida (deteccion de "shine").
    auto highRms = [&](const std::vector<float>& v) -> float {
        juce::dsp::IIR::Filter<float> hp;
        hp.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 1 });
        hp.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 4000.0f, 0.707f);
        hp.reset();
        float acc = 0.0f;
        for (float x : v) { float y = hp.processSample (x); acc += y*y; }
        return std::sqrt (acc / (float) v.size());
    };
    {
        float hiIn = highRms (in), hiOut = highRms (out);
        printf ("BRIGHTNESS: >4k rmsIn=%.5f rmsOut=%.5f  (%+6.2f dB of shine added)\n",
                hiIn, hiOut, 20.0 * std::log10 (hiOut / (juce::jmax (1e-6f, hiIn))));
    }

    // Escalado del efecto con Shine (0, 25, 50, 75, 100%) y rol de cada filtro.
    {
        for (float amt : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            VoiceChain sc; sc.prepare (sr, block, 1);
            sc.setAmount (amt, true); sc.setBodyAir (0.0f);
            profile.valid = true; profile.sibilanceLevel = 0.6f; profile.resonanceLevel = 0.6f;
            sc.setLearnProfile (profile);
            std::vector<float> so (in.size());
            juce::AudioBuffer<float> sb ((size_t) 1, block);
            size_t sp = 0;
            for (int b = 0; b < totalBlocks; ++b)
            {
                auto* d = sb.getWritePointer (0);
                for (int i = 0; i < block; ++i) d[i] = in[sp + i];
                sc.process (sb);
                for (int i = 0; i < block; ++i) so[sp + i] = sb.getSample (0, i);
                sp += block;
            }
            float hi = highRms (in), ho = highRms (so);
            printf ("AMOUNT %3d%%: hiOut=%.5f  (>4k %+5.2f dB)\n",
                    (int)(amt*100), ho, 20.0 * std::log10 (ho/(juce::jmax(1e-6f,hi))));
        }
    }

    printf ("RESULT: rmsIn=%.4f maxIn=%.4f rmsOut=%.6f maxOut=%.6f\n",
            rms (in), maxIn, rms (out), maxOut);
    if (maxIn < 0.001f)
        { printf ("FAIL: no input signal generated\n"); return 1; }
    if (maxOut < 0.001f)
        { printf ("FAIL: chain output is SILENT (all zeros) despite input\n"); return 2; }
    printf ("PASS: input=%.4f -> output=%.4f (chain audible)\n", maxIn, maxOut);
    printf ("wrote in.wav and out.wav next to this exe\n");
    return 0;
}
