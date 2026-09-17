// OPALINE CALIBRATE — corre el LearnEngine de Opaline sobre un corpus de
// referencias vocales y valida que cada perfil aprendido caiga en rango
// "de estudio" (regla del triángulo de Paco Rincón: aire ~ cuerpo - 5 dB,
// sibilancias <= techo del cuerpo 500-1k). Sirve como regresión del Learn
// contra material real y como corpus para afinar constantes.
//
// Uso:
//   OpalineCalibrate [carpeta] [segundosPorArchivo]
//   (defaults: carpeta de referencias vocales, 20 s)
#include "../../Source/DSP/LearnEngine.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

struct WavData { double sr = 0.0; std::vector<float> mono; };

static uint16_t rdU16 (const uint8_t* p) { return (uint16_t) (p[0] | (uint16_t) p[1] << 8); }
static uint32_t rdU32 (const uint8_t* p) { return p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24; }

// Lector WAV mínimo: PCM 16/24/32 y float32/64, mono o interleaved, a mono.
static bool readWav (const fs::path& path, WavData& w)
{
    FILE* f = nullptr;
    fopen_s (&f, path.string().c_str(), "rb");
    if (!f) return false;

    uint8_t hdr[12];
    if (fread (hdr, 1, 12, f) != 12 || memcmp (hdr, "RIFF", 4) != 0 || memcmp (hdr + 8, "WAVE", 4) != 0)
    {
        fclose (f);
        return false;
    }

    uint16_t audioFmt = 1, numCh = 0, bits = 0;
    uint32_t sr = 0;
    std::vector<uint8_t> samples;
    bool gotFmt = false, gotData = false;

    while (gotFmt + gotData < 2 && !ferror (f))
    {
        uint8_t chk[8];
        if (fread (chk, 1, 8, f) != 8) break;
        const uint32_t sz = rdU32 (chk + 4);
        const bool isFmt  = memcmp (chk, "fmt ", 4) == 0;
        const bool isData = memcmp (chk, "data", 4) == 0;

        if (isFmt && !gotFmt && sz >= 16)
        {
            uint8_t fmt[16];
            if (fread (fmt, 1, 16, f) != 16) { fclose (f); return false; }
            audioFmt = rdU16 (fmt + 0);
            numCh    = rdU16 (fmt + 2);
            sr       = rdU32 (fmt + 4);
            bits     = rdU16 (fmt + 14);
            gotFmt   = true;
            if (sz > 16) fseek (f, (long) (sz - 16), SEEK_CUR);
        }
        else if (isData && gotFmt && !gotData)
        {
            samples.resize (sz);
            if (sz > 0 && fread (samples.data(), 1, sz, f) != sz) { fclose (f); return false; }
            gotData = true;
        }
        else
        {
            fseek (f, (long) sz, SEEK_CUR);
        }
        if (sz & 1) fseek (f, 1, SEEK_CUR);   // chunks alineados a palabra
    }
    fclose (f);

    if (!gotFmt || !gotData || numCh == 0 || sr < 8000.0 || sr > 192000.0) return false;
    const bool isPcm = audioFmt == 1, isFlt = audioFmt == 3;
    if (!isPcm && !isFlt) return false;
    const uint32_t bytesPer = bits / 8;
    if (bytesPer == 0 || bytesPer > 8) return false;
    const uint32_t frameBytes = bytesPer * numCh;
    if (frameBytes == 0) return false;

    const uint32_t numSamples = (uint32_t) (samples.size() / frameBytes);
    if (numSamples == 0) return false;

    w.sr = sr;
    w.mono.resize (numSamples);
    size_t off = 0;
    for (uint32_t i = 0; i < numSamples; ++i, off += frameBytes)
    {
        float acc = 0.0f;
        for (uint16_t c = 0; c < numCh; ++c)
        {
            const uint8_t* sp = samples.data() + off + (size_t) c * bytesPer;
            float v = 0.0f;
            if (isFlt)
            {
                if (bits == 32) { float t; memcpy (&t, sp, 4); v = t; }
                else if (bits == 64) { double t; memcpy (&t, sp, 8); v = (float) t; }
            }
            else if (bits == 16)
            {
                v = (float) ((int16_t) rdU16 (sp) / 32768.0);
            }
            else if (bits == 24)
            {
                const int32_t t = (sp[0] | (int32_t) sp[1] << 8 | (int32_t) sp[2] << 16) << 8 >> 8;
                v = (float) ((double) t / 8388608.0);
            }
            else if (bits == 32)
            {
                const int32_t t = (int32_t) rdU32 (sp);
                v = (float) ((double) t / 2147483648.0);
            }
            acc += v;
        }
        w.mono[i] = acc / (float) numCh;
    }
    return true;
}

// Rango "pro-vocal": avisos fuera de lo que una voz de estudio comercial cumple.
// El exceso de sibilancias solo se avisa en material VOCAL puro (los mixes llevan
// percusión/arpegios que pueblan 5-10k y siempre muestran exceso 15-24 dB).
static int sanity (const LearnProfile& p, const char* kind, char* out, size_t outSz)
{
    int n = 0;
    char b[256];
    auto add = [&](const char* line)
    {
        if (n < 6) n += snprintf (out + n, outSz - (size_t) n, "%s", line);
    };
    if (p.medianPitchHz < 60.0 || p.medianPitchHz > 800.0)
    {
        snprintf (b, sizeof b, "    WARN f0 mediana %.0f Hz fuera de rango (mezcla con instrumentos?)\n", p.medianPitchHz); add (b);
    }
    if (p.airTargetDb > 9.0)
    {
        snprintf (b, sizeof b, "    WARN presencia pide +%.1f dB (>9): senal apagada\n", p.airTargetDb); add (b);
    }
    if (p.airTargetDb < -8.0)
    {
        snprintf (b, sizeof b, "    WARN presencia de sobra %.1f dB (<-8): senal dura\n", p.airTargetDb); add (b);
    }
    const bool isVocal = juce::String (kind).contains ("DRYLEAD") || juce::String (kind).contains ("lead");
    if (isVocal && p.sibExcessDb > 26.0)
    {
        snprintf (b, sizeof b, "    WARN vocal con exceso de sibilancias %.1f dB (>26)\n", p.sibExcessDb); add (b);
    }
    return n;
}

int main (int argc, char** argv)
{
    const char* folder = argc > 1 ? argv[1] : "G:/MARCELO/REFERENCIAS/Vocal References/Vocal References";
    const double capSec = argc > 2 ? std::atof (argv[2]) : 20.0;

    if (!fs::exists (folder) || !fs::is_directory (folder))
    {
        printf ("FATAL: no existe la carpeta: %s\n", folder);
        return 1;
    }

    std::vector<fs::path> files;
    for (auto& e : fs::directory_iterator (folder))
    {
        auto ext = e.path().extension().string();
        std::transform (ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".wav") files.push_back (e.path());
    }
    std::sort (files.begin(), files.end(), [](const fs::path& a, const fs::path& b)
    {
        return a.filename().string() < b.filename().string();
    });

    printf ("== OPALINE CALIBRATE ==\n");
    printf ("carpeta  : %s\n", folder);
    printf ("analisis : %.0f s por archivo (inicio) en chunks de 8192\n", capSec);
    printf ("\n");
    printf ("%-52s %3s %7s %6s %6s %7s %7s  %5s %5s %s\n",
            "ARCHIVO", "win", "med", "deEss", "res", "airTgt", "sibExc", "sib", "brt", "V");

    int ok = 0, warned = 0, failed = 0;
    for (auto& path : files)
    {
        WavData w;
        if (!readWav (path, w) || w.mono.empty())
        {
            ++failed;
            printf ("%-52s    FAIL no legible\n", path.filename().string().c_str());
            continue;
        }

        LearnEngine learn;
        learn.prepare (w.sr, 8192);
        learn.setMeasuring (true);

        const size_t cap = (size_t) std::min<double> ((double) w.mono.size(), capSec * w.sr);
        juce::AudioBuffer<float> buf (1, 8192);
        size_t pos = 0;
        while (pos < cap)
        {
            const int n = (int) std::min<size_t> (8192, cap - pos);
            std::memcpy (buf.getWritePointer (0), w.mono.data() + pos, (size_t) n * sizeof (float));
            learn.process (buf, true);
            pos += (size_t) n;
        }

        const long long win = learn.getAnalyzedWindows();
        const LearnProfile p = learn.finish();

        std::string name = path.filename().string();
        std::string kind = "mix ";
        if (name.find ("Dry Lead") != std::string::npos) kind = "DRYLEAD";
        else if (name.find ("(Lead)") != std::string::npos) kind = "lead ";
        else if (name.find ("Backing)") != std::string::npos) kind = "back ";

        char warnBuf[512] = { 0 };
        const int nWarn = sanity (p, kind.c_str(), warnBuf, sizeof warnBuf);

        printf ("%-52s %3lld %7.0f %6.0f %6.0f %+7.1f %7.1f  %5.2f %5.2f  %s%s%s\n",
                name.substr (0, 52).c_str(),
                win,
                p.medianPitchHz, p.deEssFreq, p.resonanceFreq,
                (double) p.airTargetDb, (double) p.sibExcessDb,
                (double) p.sibilanceLevel, (double) p.brightness,
                p.valid ? "" : "EDef=",
                kind.c_str(), nWarn == 0 && p.valid ? "OK" : "WARN");
        if (nWarn > 0 || !p.valid)
        {
            if (!p.valid) printf ("    WARN perfil NO valido (el motor no acumulo energia)\n");
            fputs (warnBuf, stdout);
            ++warned;
        }
        else
            ++ok;
    }

    printf ("\nRESULTADO: %d en rango pro, %d con avisos, %d ilegibles (de %zu)\n", ok, warned, failed, files.size());

    // Sonda de block-size: verifica el FIX — con 128..2048..8192 el motor debe
    // acumular siempre (ring buffer interno), a diferencia del bug previo donde
    // bloques < 2048 no generaban ninguna ventana FFT.
    if (!files.empty())
    {
        WavData w;
        if (readWav (files[0], w))
        {
            printf ("\n== SONDA block-size (archivo: %s) ==\n", files[0].filename().string().c_str());
            for (int bs : { 128, 256, 512, 1024, 2048, 8192 })
            {
                LearnEngine le;
                le.prepare (w.sr, bs);
                le.setMeasuring (true);
                const size_t cap = (size_t) std::min<double> ((double) w.mono.size(), capSec * w.sr);
                juce::AudioBuffer<float> bb (1, bs);
                size_t pos = 0;
                while (pos < cap)
                {
                    const int n = (int) std::min<size_t> (bs, cap - pos);
                    std::memcpy (bb.getWritePointer (0), w.mono.data() + pos, (size_t) n * sizeof (float));
                    le.process (bb, true);
                    pos += (size_t) n;
                }
                const long long win = le.getAnalyzedWindows();
                LearnProfile pp = le.finish();
                printf ("  bs=%4d -> ventanas FFT=%4lld  valid=%d  deEss=%5.0f Hz\n",
                        bs, win, pp.valid ? 1 : 0, pp.deEssFreq);
            }
        }
    }

    printf ("\nLos pares 'Dry Lead Only' (Dua Lipa x2) son la unica comparacion 1:1; el resto son mezclas\n");
    printf ("con instrumentos que inflan el espectro (esperar airTgt/sibExc mas altos de lo vocal-puro).\n");
    return 0;
}