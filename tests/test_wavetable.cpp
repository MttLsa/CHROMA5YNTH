#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "engines/wavetable/WavetableDoc.h"
#include "engines/wavetable/WavetableEngine.h"
#include "TestHelpers.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <chrono>
#include <cmath>
#include <complex>
#include <vector>

using chroma::WavetableEngine;
namespace lp = chroma::lp;
namespace wt = chroma::wt;

namespace
{
constexpr double fs = 48000.0;
constexpr double pi = 3.14159265358979323846;

int tableIndex (const char* name)
{
    for (int i = 0; i < wt::numFactoryTables(); ++i)
        if (std::string (wt::factoryName (i)) == name) return i;
    return -1;
}

struct WtRig
{
    WavetableEngine engine;
    std::array<float, lp::count> base {};
    chroma::EngineVoiceParams p;

    explicit WtRig (int oversampling = 2)
    {
        for (int i = 0; i < lp::count; ++i)
            base[(size_t) i] = lp::descs[(size_t) i].def;
        engine.prepare (fs, 32, 1);
        engine.setOversampling (oversampling);
        p.base = base.data();
        set (lp::wtOscVolume[0], 1.0f);
    }
    void set (int id, float v) { base[(size_t) id] = v; }
    void table (int k, const char* name) { set (lp::wtOscTable[(size_t) k], (float) tableIndex (name)); }
    void note (double hz)
    {
        p.pitchStart = p.pitchEnd = (float) (69.0 + 12.0 * std::log2 (hz / 440.0));
        engine.noteOn (0, {});
    }
    void render (std::vector<float>& l, std::vector<float>& r, int count, int skip = 4800)
    {
        l.assign ((size_t) count, 0.0f);
        r.assign ((size_t) count, 0.0f);
        std::array<float, 32> tl {}, tr {};
        for (int pos = -skip; pos < count; pos += 32)
        {
            const int n = std::min (32, count - pos);
            engine.render (0, p, tl.data(), tr.data(), n);
            for (int i = 0; i < n; ++i)
                if (pos + i >= 0) { l[(size_t) (pos + i)] = tl[(size_t) i]; r[(size_t) (pos + i)] = tr[(size_t) i]; }
        }
    }
};

double rms (const std::vector<float>& x)
{
    double s = 0.0;
    for (float v : x) s += (double) v * v;
    return std::sqrt (s / (double) std::max<size_t> (1, x.size()));
}

// Amplitude of a sinusoid at hz (single-bin DFT, Hann window, n samples).
double toneAmp (const std::vector<float>& x, double hz)
{
    std::complex<double> acc;
    double wsum = 0.0;
    const size_t n = x.size();
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) n);
        acc += w * (double) x[i] * std::polar (1.0, -2.0 * pi * hz * (double) i / fs);
        wsum += w;
    }
    return 2.0 * std::abs (acc) / wsum;
}

// Power-spectrum split: energy at harmonics of f0 vs everything else (Blackman-Harris, radix-2 FFT).
void harmonicSplit (const std::vector<float>& x, double f0, double& harm, double& other, double maxHz = 24000.0)
{
    size_t n = 1;
    while (n * 2 <= x.size()) n *= 2;
    std::vector<std::complex<double>> a (n);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = 2.0 * pi * (double) i / (double) n;
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        a[i] = w * (double) x[i];
    }
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const std::complex<double> wl = std::polar (1.0, -2.0 * pi / (double) len);
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    harm = other = 0.0;
    const double binHz = fs / (double) n;
    for (size_t k = 1; k < n / 2; ++k)
    {
        const double hz = (double) k * binHz;
        if (hz > maxHz) break;
        const double pwr = std::norm (a[k]);
        const double h = hz / f0;
        if (hz < 0.5 * f0) continue;   // DC (warps are asymmetric)
        if (std::fabs (h - std::round (h)) * f0 < 6.0 * binHz && std::round (h) >= 1.0) harm += pwr;
        else other += pwr;
    }
}

// Energy above `hz` relative to total (DFT by Goertzel on harmonics of f0).
double highRatio (const std::vector<float>& x, double f0, int fromHarmonic)
{
    double hi = 0.0, all = 0.0;
    for (int k = 1; k * f0 < 20000.0; ++k)
    {
        const double a = toneAmp (x, k * f0);
        all += a * a;
        if (k >= fromHarmonic) hi += a * a;
    }
    return hi / std::max (all, 1.0e-30);
}
} // namespace

TEST_CASE ("Wavetable: factory bank (>= 40 procedural tables) matches the parameter list", "[wavetable]")
{
    const auto& bank = wt::factory();
    REQUIRE (wt::numFactoryTables() >= 40);
    REQUIRE ((int) bank.tables.size() == wt::numFactoryTables());
    const auto& d = lp::descs[(size_t) lp::wtOscTable[0]];
    REQUIRE (d.numChoices == wt::numFactoryTables() + wt::numUserSlots);
    for (int i = 0; i < wt::numFactoryTables(); ++i)
    {
        CHECK (std::string (d.choices[i]) == wt::factoryName (i));
        const auto& t = *bank.tables[(size_t) i];
        REQUIRE (t.numFrames == 32);
        for (float v : t.mip) REQUIRE (std::isfinite (v));
        float peak = 0.0f;
        for (int f = 0; f < t.numFrames; ++f)
            for (int s = 0; s < wt::levelLength (0); ++s) peak = std::max (peak, std::fabs (t.level (f, 0)[s]));
        CHECK (peak == Catch::Approx (0.99f).margin (1e-3));
    }
    for (int u = 0; u < wt::numUserSlots; ++u)
        CHECK (std::string (d.choices[wt::numFactoryTables() + u]) == "User " + std::to_string (u + 1));
    REQUIRE (bank.sub->numFrames == 4);
}

TEST_CASE ("Wavetable: mipmaps are band-limited and analysis/synthesis round-trips", "[wavetable]")
{
    // A saw frame: level l keeps harmonics 1..levelHarmonics(l) exactly and nothing above.
    std::vector<float> saw ((size_t) wt::frameSize);
    for (int i = 0; i < wt::frameSize; ++i) saw[(size_t) i] = 2.0f * (float) i / (float) wt::frameSize - 1.0f;
    auto t = wt::buildTable (saw.data(), 1, "saw", false);
    std::vector<std::complex<float>> c ((size_t) wt::specSize);
    for (int l = 0; l < wt::numLevels; ++l)
    {
        const int len = wt::levelLength (l), H = wt::levelHarmonics (l);
        const float* x = t->level (0, l);
        REQUIRE (x[len] == Catch::Approx (x[0]));   // guard sample
        for (int k : { 1, H, H + 1, std::min (len / 2 - 1, 2 * H + 3) })
        {
            std::complex<double> acc;
            for (int i = 0; i < len; ++i) acc += (double) x[i] * std::polar (1.0, -2.0 * pi * k * i / len);
            const double a = 2.0 * std::abs (acc) / len;
            const double expect = k <= H ? 2.0 / (pi * k) : 0.0;
            CHECK (a == Catch::Approx (expect).margin (2.0e-3));
        }
    }
    // spectrum convention: x(t) = sum Re(c_k e^{i 2 pi k t})
    wt::analyse (saw.data(), c.data());
    CHECK (std::abs (c[1]) == Catch::Approx (2.0 / pi).margin (1e-3));
    CHECK (std::abs (c[3]) == Catch::Approx (2.0 / (3.0 * pi)).margin (1e-3));
}

TEST_CASE ("Wavetable: pitch and position morph", "[wavetable]")
{
    WtRig r;
    r.table (0, "Basic Shapes");
    r.set (lp::wtOscPos[0], 0.0f);   // sine
    r.note (440.0);
    std::vector<float> l, rr;
    r.render (l, rr, 8192);
    const double fund = toneAmp (l, 440.0);
    CHECK (fund > 0.3);
    CHECK (toneAmp (l, 880.0) < fund * 1.0e-3);
    CHECK (toneAmp (l, 1320.0) < fund * 1.0e-3);
    CHECK (toneAmp (l, 452.0) < fund * 0.05);   // on pitch

    r.set (lp::wtOscPos[0], 0.5f);   // ~saw
    r.note (440.0);
    r.render (l, rr, 8192);
    CHECK (toneAmp (l, 880.0) > 0.2 * toneAmp (l, 440.0));

    // octave / semitone
    r.set (lp::wtOscPos[0], 0.0f);
    r.set (lp::wtOscOctave[0], 1.0f);
    r.set (lp::wtOscSemi[0], 7.0f);
    r.note (220.0);
    r.render (l, rr, 8192);
    CHECK (toneAmp (l, 220.0 * 2.0 * std::pow (2.0, 7.0 / 12.0)) > 0.3);
}

TEST_CASE ("Wavetable: aliasing stays low (mipmaps + 2x oversampling)", "[wavetable]")
{
    for (double hz : { 440.0, 1760.0, 3520.0, 7040.0 })
    {
        WtRig r (2);
        r.table (0, "Saw Sync");    // frame 0 = plain saw
        r.note (hz);
        std::vector<float> l, rr;
        r.render (l, rr, 32768);
        double h = 0.0, o = 0.0;
        harmonicSplit (l, hz, h, o);
        INFO ("f0 " << hz << " Hz: alias " << 10.0 * std::log10 (o / h) << " dB");
        CHECK (10.0 * std::log10 (o / h) < -65.0);
    }
    {
        WtRig r (4);
        r.table (0, "Saw Sync");
        r.note (440.0);
        std::vector<float> l, rr;
        r.render (l, rr, 32768);
        double h = 0.0, o = 0.0;
        harmonicSplit (l, 440.0, h, o);
        INFO ("4x alias " << 10.0 * std::log10 (o / h) << " dB");
        CHECK (10.0 * std::log10 (o / h) < -80.0);
    }
    // warp modes add harmonics: still bounded and alias-controlled at 4x
    for (int mode : { (int) WavetableEngine::Sync, (int) WavetableEngine::BendPlus, (int) WavetableEngine::Asym })
    {
        WtRig r (4);
        r.table (0, "Basic Shapes");
        r.set (lp::wtOscWarp[0], (float) mode);
        r.set (lp::wtOscWarpAmount[0], 0.5f);
        r.note (1000.0);
        std::vector<float> l, rr;
        r.render (l, rr, 32768);
        double h = 0.0, o = 0.0;
        harmonicSplit (l, 1000.0, h, o);
        INFO ("warp " << mode << ": alias " << 10.0 * std::log10 (o / h) << " dB");
        CHECK (10.0 * std::log10 (o / h) < -40.0);
    }
}

TEST_CASE ("Wavetable: warp modes", "[wavetable]")
{
    bool silent = false;
    CHECK (WavetableEngine::warpPhase (WavetableEngine::BendPlus, 0.0, 0.7f, 0.0f, silent) == Catch::Approx (0.0));
    CHECK (WavetableEngine::warpPhase (WavetableEngine::BendPlus, 1.0, 0.7f, 0.0f, silent) == Catch::Approx (1.0));
    CHECK (WavetableEngine::warpPhase (WavetableEngine::BendPlus, 0.25, 0.7f, 0.0f, silent) > 0.25);
    CHECK (WavetableEngine::warpPhase (WavetableEngine::BendMinus, 0.25, 0.7f, 0.0f, silent) < 0.25);
    CHECK (WavetableEngine::warpPhase (WavetableEngine::BendBoth, 0.5, 0.7f, 0.0f, silent) == Catch::Approx (0.5));
    CHECK (WavetableEngine::warpPhase (WavetableEngine::Sync, 0.5, 1.0f, 0.0f, silent) == Catch::Approx (0.0).margin (1e-9));
    CHECK (WavetableEngine::warpPhase (WavetableEngine::Quantize, 0.33, 1.0f, 0.0f, silent) == Catch::Approx (0.0));
    CHECK (WavetableEngine::warpPhase (WavetableEngine::Fm, 0.1, 0.5f, 0.2f, silent) == Catch::Approx (0.3));
    CHECK (! silent);
    WavetableEngine::warpPhase (WavetableEngine::Pwm, 0.9, 0.5f, 0.0f, silent);
    CHECK (silent);

    // every mode renders finite, bounded audio that differs from the unwarped tone
    WtRig ref;
    ref.table (0, "Basic Shapes");
    ref.note (220.0);
    std::vector<float> l0, r0;
    ref.render (l0, r0, 4096);
    for (int mode = 1; mode <= (int) WavetableEngine::Ring; ++mode)
    {
        WtRig r;
        r.table (0, "Basic Shapes");
        r.set (lp::wtSubOn, 1.0f);
        r.set (lp::wtSubLevel, 0.0f);   // Sub only as warp source
        r.set (lp::wtOscWarp[0], (float) mode);
        r.set (lp::wtOscWarpAmount[0], 0.8f);
        r.note (220.0);
        std::vector<float> l, rr;
        r.render (l, rr, 4096);
        double diff = 0.0, peak = 0.0;
        for (size_t i = 0; i < l.size(); ++i)
        {
            REQUIRE (std::isfinite (l[i]));
            diff += std::fabs (l[i] - l0[i]);
            peak = std::max (peak, (double) std::fabs (l[i]));
        }
        INFO ("mode " << mode);
        CHECK (peak < 4.0);
        CHECK (diff / (double) l.size() > 0.01);
    }

    // FM from OSC 2 (one OSC modulating another), and an OSC used as its own source (one block delay)
    WtRig r;
    r.table (0, "Basic Shapes");
    r.set (lp::wtOscOn[1], 1.0f);
    r.set (lp::wtOscVolume[1], 0.0f);
    r.set (lp::wtOscWarp[0], (float) WavetableEngine::Fm);
    r.set (lp::wtOscWarpAmount[0], 0.5f);
    r.set (lp::wtOscWarpSrc[0], 1.0f);
    r.note (220.0);
    std::vector<float> l, rr;
    r.render (l, rr, 8192);
    CHECK (toneAmp (l, 440.0) > 0.05);   // FM sidebands
}

TEST_CASE ("Wavetable: spectral processing (tilt, formant, freeze, blur, stretch)", "[wavetable]")
{
    const auto renderWith = [] (int id, float v, std::vector<float>& l, float pos = 0.5f)
    {
        WtRig r;
        r.table (0, "Basic Shapes");
        r.set (lp::wtOscPos[0], pos);
        r.set (id, v);
        r.note (110.0);
        std::vector<float> rr;
        r.render (l, rr, 16384);
    };
    std::vector<float> plain, dark, bright, shifted, stretched, blurred;
    renderWith (lp::wtOscTilt[0], 0.0f, plain);
    renderWith (lp::wtOscTilt[0], -1.0f, dark);
    renderWith (lp::wtOscTilt[0], 0.5f, bright);
    const double hPlain = highRatio (plain, 110.0, 8), hDark = highRatio (dark, 110.0, 8), hBright = highRatio (bright, 110.0, 8);
    CHECK (hDark < hPlain * 0.2);
    CHECK (hBright > hPlain * 1.5);
    CHECK (rms (dark) == Catch::Approx (rms (plain)).epsilon (0.5));   // loudness compensated

    renderWith (lp::wtOscFormant[0], 0.5f, shifted);
    CHECK (highRatio (shifted, 110.0, 8) > hPlain * 1.2);
    renderWith (lp::wtOscStretch[0], 1.0f, stretched);
    CHECK (std::fabs (highRatio (stretched, 110.0, 8) - hPlain) > 0.01);
    renderWith (lp::wtOscBlur[0], 1.0f, blurred);
    for (float v : blurred) REQUIRE (std::isfinite (v));

    // Freeze: moving the position after the note start does not change the magnitude spectrum.
    WtRig r;
    r.table (0, "Basic Shapes");
    r.set (lp::wtOscPos[0], 0.0f);      // sine at note start
    r.set (lp::wtOscFreeze[0], 1.0f);
    r.note (110.0);
    std::vector<float> l, rr;
    r.render (l, rr, 64, 0);
    r.set (lp::wtOscPos[0], 0.75f);     // square-ish
    r.render (l, rr, 16384);
    CHECK (highRatio (l, 110.0, 2) < 0.01);
}

TEST_CASE ("Wavetable: unison, detune, random phase, advanced time-domain processing", "[wavetable]")
{
    WtRig r;
    r.table (0, "Saw Sync");
    r.set (lp::wtOscUnison[0], 16.0f);
    r.set (lp::wtOscUniDetune[0], 40.0f);
    r.set (lp::wtOscUniSpread[0], 1.0f);
    r.set (lp::wtOscRandPhase[0], 1.0f);
    r.note (220.0);
    std::vector<float> l, rr;
    r.render (l, rr, 9600);
    double diff = 0.0;
    for (size_t i = 0; i < l.size(); ++i) { REQUIRE (std::isfinite (l[i])); diff += std::fabs (l[i] - rr[i]); }
    CHECK (rms (l) > 0.1);
    CHECK (diff / (double) l.size() > 0.01);

    // Width 0 = mono
    r.set (lp::wtOscUniWidth[0], 0.0f);
    r.render (l, rr, 4800);
    diff = 0.0;
    for (size_t i = 0; i < l.size(); ++i) diff += std::fabs (l[i] - rr[i]);
    CHECK (diff / (double) l.size() < 1.0e-4);

    // Detune (Hz): constant beat
    WtRig d;
    d.table (0, "Basic Shapes");
    d.set (lp::wtOscDetune[0], 5.0f);
    d.note (440.0);
    d.render (l, rr, 16384);
    CHECK (toneAmp (l, 445.0) > 5.0 * toneAmp (l, 440.0 - 5.0));

    for (int id : { lp::wtOscPd[0], lp::wtOscBits[0], lp::wtOscSrr[0], lp::wtOscGrain[0] })
    {
        WtRig a;
        a.table (0, "Vowels");
        a.set (lp::wtOscPos[0], 0.3f);
        a.set (id, id == lp::wtOscBits[0] ? 3.0f : 0.8f);
        a.note (220.0);
        std::vector<float> x, y;
        a.render (x, y, 9600);
        WtRig b;
        b.table (0, "Vowels");
        b.set (lp::wtOscPos[0], 0.3f);
        b.note (220.0);
        std::vector<float> x0, y0;
        b.render (x0, y0, 9600);
        double dd = 0.0;
        for (size_t i = 0; i < x.size(); ++i) { REQUIRE (std::isfinite (x[i])); dd += std::fabs (x[i] - x0[i]); }
        INFO ("param " << lp::descs[(size_t) id].id);
        CHECK (dd / (double) x.size() > 0.005);
        CHECK (rms (x) > 0.05);
    }

    // Scan sweeps the table over time
    WtRig s;
    s.table (0, "Basic Shapes");
    s.set (lp::wtOscScan[0], 1.0f);
    s.note (110.0);
    std::vector<float> a1, b1, a2, b2;
    s.render (a1, b1, 2048, 0);
    s.render (a2, b2, 2048, 3000);
    CHECK (highRatio (a2, 110.0, 4) > highRatio (a1, 110.0, 4) + 0.01);
}

TEST_CASE ("Wavetable: sub, noise, zero CPU when silent", "[wavetable]")
{
    WtRig r;
    r.set (lp::wtOscOn[0], 0.0f);
    r.set (lp::wtSubOn, 1.0f);
    r.set (lp::wtSubLevel, 1.0f);
    r.set (lp::wtSubOctave, -1.0f);
    r.note (440.0);
    std::vector<float> l, rr;
    r.render (l, rr, 8192);
    CHECK (toneAmp (l, 220.0) > 0.5);
    CHECK (toneAmp (l, 440.0) < 0.01);

    WtRig n;
    n.set (lp::wtOscOn[0], 0.0f);
    n.set (lp::wtNoiseLevel, 1.0f);
    n.note (440.0);
    n.render (l, rr, 8192);
    CHECK (rms (l) > 0.2);

    WtRig z;
    z.set (lp::wtOscOn[0], 0.0f);
    z.note (440.0);
    z.render (l, rr, 4096);
    for (float v : l) REQUIRE (std::fabs (v) < 1.0e-30f);
}

TEST_CASE ("Wavetable editor model: WAV I/O, frames, interpolation, harmonics", "[wavetable]")
{
    wt::WavetableDoc doc;
    REQUIRE (doc.numFrames() == 1);
    int nf = 0;
    auto frames = wt::factoryFrames (tableIndex ("Basic Shapes"), nf);
    doc.setFrames (frames, nf);
    REQUIRE (doc.numFrames() == 32);

    // WAV round trip (32-bit float)
    juce::MemoryBlock mb;
    REQUIRE (doc.exportWav (new juce::MemoryOutputStream (mb, false)));
    wt::WavetableDoc back;
    juce::String err;
    REQUIRE (back.importWav (new juce::MemoryInputStream (mb, false), err));
    REQUIRE (back.numFrames() == 32);
    for (size_t i = 0; i < frames.size(); i += 97)
        REQUIRE (back.frames()[i] == Catch::Approx (frames[i]).margin (1e-6));

    // single-cycle import (shorter than 2048) is resampled to one frame
    {
        juce::MemoryBlock m2;
        juce::WavAudioFormat wav;
        auto* os = new juce::MemoryOutputStream (m2, false);
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os, 48000.0, 1, 24, {}, 0));
        std::vector<float> cyc (600);
        for (size_t i = 0; i < cyc.size(); ++i) cyc[i] = (float) std::sin (2.0 * pi * (double) i / 600.0);
        const float* ch[1] = { cyc.data() };
        w->writeFromFloatArrays (ch, 1, 600);
        w.reset();
        wt::WavetableDoc one;
        REQUIRE (one.importWav (new juce::MemoryInputStream (m2, false), err));
        REQUIRE (one.numFrames() == 1);
        CHECK (one.frame (0)[512] == Catch::Approx (1.0).margin (0.01));
    }

    // frames
    doc.addFrame (0);
    CHECK (doc.numFrames() == 33);
    doc.removeFrame (0);
    doc.duplicateFrame (31);
    CHECK (doc.numFrames() == 33);
    doc.removeFrame (32);

    // resample linear / spectral / none
    wt::WavetableDoc two;
    std::vector<float> ab ((size_t) 2 * wt::frameSize);
    for (int i = 0; i < wt::frameSize; ++i)
    {
        ab[(size_t) i] = (float) std::sin (2.0 * pi * i / wt::frameSize);
        ab[(size_t) (wt::frameSize + i)] = (float) std::sin (2.0 * pi * i / wt::frameSize + pi);   // inverted
    }
    two.setFrames (ab, 2);
    auto lin = two;
    lin.resample (3, wt::WavetableDoc::Interp::Linear);
    REQUIRE (lin.numFrames() == 3);
    float pk = 0.0f;
    for (int i = 0; i < wt::frameSize; ++i) pk = std::max (pk, std::fabs (lin.frame (1)[i]));
    CHECK (pk < 1.0e-4f);   // linear: cancellation
    auto spec = two;
    spec.resample (3, wt::WavetableDoc::Interp::Spectral);
    pk = 0.0f;
    for (int i = 0; i < wt::frameSize; ++i) pk = std::max (pk, std::fabs (spec.frame (1)[i]));
    CHECK (pk == Catch::Approx (1.0f).margin (0.02));   // spectral: magnitude kept
    auto none = two;
    none.resample (4, wt::WavetableDoc::Interp::None);
    CHECK (none.frame (1)[512] == Catch::Approx (1.0f).margin (1e-3));

    // morph between first and last frame
    auto m = lin;
    m.frame (1)[0] = 5.0f;
    m.morph (0, 2, wt::WavetableDoc::Interp::Linear);
    CHECK (m.frame (1)[0] == Catch::Approx (0.0f).margin (1e-4));

    // smoothing removes a spike, normalize to peak 1 without DC
    wt::WavetableDoc s;
    std::vector<float> spike ((size_t) wt::frameSize, 0.25f);
    spike[100] = 1.0f;
    s.setFrames (spike, 1);
    s.smooth (0, 1.0f);
    CHECK (s.frame (0)[100] < 0.5f);
    s.normalize (true);
    double dc = 0.0;
    pk = 0.0f;
    for (int i = 0; i < wt::frameSize; ++i) { dc += s.frame (0)[i]; pk = std::max (pk, std::fabs (s.frame (0)[i])); }
    CHECK (std::fabs (dc / wt::frameSize) < 1.0e-4);
    CHECK (pk == Catch::Approx (1.0f));

    // draw
    s.drawLine (0, 0.0f, -1.0f, 0.5f, 1.0f);
    CHECK (s.frame (0)[0] == Catch::Approx (-1.0f));
    CHECK (s.frame (0)[1023] == Catch::Approx (1.0f).margin (0.01));

    // harmonic editor
    wt::WavetableDoc h;
    std::vector<float> amp, ph;
    h.harmonics (0, 8, amp, ph);
    CHECK (amp[0] == Catch::Approx (1.0f).margin (1e-3));
    CHECK (ph[0] == Catch::Approx (0.0f).margin (1e-3));
    h.setHarmonic (0, 3, 0.5f, (float) (pi / 2.0));
    h.harmonics (0, 8, amp, ph);
    CHECK (amp[2] == Catch::Approx (0.5f).margin (1e-3));
    CHECK (ph[2] == Catch::Approx (pi / 2.0).margin (1e-3));
    CHECK (h.frame (0)[0] == Catch::Approx (0.5f).margin (1e-3));   // 0.5 sin(3*0 + pi/2)

    // build -> usable table
    auto t = h.build ("User 1");
    REQUIRE (t->numFrames == 1);
}

TEST_CASE ("Wavetable: user tables through the synth (publish, state round trip)", "[wavetable]")
{
    test::Rig rig;
    test::setParam (rig.proc, "layer1_engine", 3.0f);   // Wavetable
    auto& store = rig.synth().layerStore (0);
    std::vector<float> fr ((size_t) 2 * wt::frameSize);
    for (int i = 0; i < wt::frameSize; ++i)
    {
        fr[(size_t) i] = (float) std::sin (2.0 * pi * i / wt::frameSize);
        fr[(size_t) (wt::frameSize + i)] = i < wt::frameSize / 2 ? 0.8f : -0.8f;
    }
    rig.synth().setUserWavetable (0, 1, fr, 2);
    REQUIRE (store.userTableSlots()[1].get() != nullptr);
    REQUIRE (store.userTableSlots()[1].get()->numFrames == 2);
    store.set (lp::wtOscTable[0], (float) (wt::numFactoryTables() + 1));

    rig.noteOn (57);
    float peak = 0.0f;
    for (int b = 0; b < 40; ++b)
    {
        rig.run();
        for (int i = 0; i < rig.buffer.getNumSamples(); ++i) peak = std::max (peak, std::fabs (rig.buffer.getSample (0, i)));
    }
    CHECK (peak > 0.05f);

    // state round trip keeps the user table
    const auto state = rig.synth().saveState();
    rig.synth().setUserWavetable (0, 1, {}, 0);
    REQUIRE (store.userTableSlots()[1].get() == nullptr);
    rig.synth().loadState (state);
    REQUIRE (store.userTable (1).numFrames == 2);
    CHECK (store.userTable (1).frames[(size_t) wt::frameSize + 10] == Catch::Approx (0.8f));
    REQUIRE (store.userTableSlots()[1].get() != nullptr);

    // layer copy/paste carries it too
    const auto layer = rig.synth().copyLayer (0);
    rig.synth().pasteLayer (2, layer);
    CHECK (rig.synth().layerStore (2).userTable (1).numFrames == 2);
}

TEST_CASE ("Wavetable: Filter 2 with serial / parallel routing in the voice", "[wavetable]")
{
    const auto play = [] (bool f2, int routing, float mix)
    {
        test::Rig rig;
        test::setParam (rig.proc, "layer1_engine", 3.0f);
        auto& s = rig.synth().layerStore (0);
        s.set (lp::wtOscTable[0], (float) tableIndex ("Saw Sync"));
        s.set (lp::wtF2On, f2 ? 1.0f : 0.0f);
        s.set (lp::wtF2Model, 12.0f);       // Digital LP12
        s.set (lp::wtF2Cutoff, 200.0f);
        s.set (lp::wtFilterRouting, (float) routing);
        s.set (lp::wtFilterMix, mix);
        rig.noteOn (69);
        rig.run ({}, 20);
        std::vector<float> x;
        for (int b = 0; b < 64; ++b)
        {
            rig.run();
            for (int i = 0; i < rig.buffer.getNumSamples(); ++i) x.push_back (rig.buffer.getSample (0, i));
        }
        return x;
    };
    const auto dry = play (false, 0, 1.0f);
    const auto serial = play (true, 0, 1.0f);
    const auto parallelF1 = play (true, 1, 0.0f);   // Filter 1 (off = passthrough) only
    const auto parallelF2 = play (true, 1, 1.0f);
    const double hDry = highRatio (dry, 440.0, 4);
    CHECK (rms (dry) > 0.05);
    CHECK (highRatio (serial, 440.0, 4) < hDry * 0.1);
    CHECK (highRatio (parallelF2, 440.0, 4) < hDry * 0.1);
    CHECK (highRatio (parallelF1, 440.0, 4) == Catch::Approx (hDry).epsilon (0.1));
}

TEST_CASE ("Wavetable: CPU benchmark", "[.bench]")
{
    WtRig r;
    for (int k = 0; k < 3; ++k)
    {
        r.set (lp::wtOscOn[(size_t) k], 1.0f);
        r.set (lp::wtOscUnison[(size_t) k], 4.0f);
        r.set (lp::wtOscTilt[(size_t) k], k == 2 ? 0.3f : 0.0f);
    }
    r.note (220.0);
    std::vector<float> l, rr;
    const auto t0 = std::chrono::steady_clock::now();
    r.render (l, rr, 48000, 0);
    const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    WARN ("WT voice, 3 OSC x 4 unison (1 spectral), 2x: " << sec * 100.0 << " % core @48 kHz");
}
