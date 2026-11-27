#include <catch2/catch_test_macros.hpp>
#include "engines/analog/AnalogEngine.h"
#include "TestHelpers.h"
#include <cmath>
#include <complex>
#include <vector>

using chroma::AnalogEngine;
namespace lp = chroma::lp;

namespace
{
constexpr double fs = 48000.0;
constexpr int fftSize = 65536;
constexpr double pi = 3.14159265358979323846;

struct AnalogRig
{
    AnalogEngine engine;
    std::array<float, lp::count> base {};
    chroma::EngineVoiceParams p;
    std::vector<float> byL = std::vector<float> (32), byR = std::vector<float> (32);
    bool bypassUsed = false;

    explicit AnalogRig (int oversampling = 4)
    {
        for (int i = 0; i < lp::count; ++i)
            base[(size_t) i] = lp::descs[(size_t) i].def;
        engine.prepare (fs, 32, 1);
        engine.setOversampling (oversampling);
        p.base = base.data();
        set (lp::anMasterLevel, 1.0f);
        set (lp::anOscLevel[0], 1.0f);
    }
    void set (int id, float v) { base[(size_t) id] = v; }
    void note (double hz) { p.pitchStart = p.pitchEnd = (float) (69.0 + 12.0 * std::log2 (hz / 440.0)); engine.noteOn (0, {}); }

    // Renders `count` samples (filter bus, or bypass bus) after `skip` samples.
    void render (std::vector<float>& l, std::vector<float>& r, int count, int skip = 4800, bool bypassBus = false)
    {
        l.assign ((size_t) count, 0.0f);
        r.assign ((size_t) count, 0.0f);
        std::array<float, 32> tl {}, tr {};
        for (int pos = -skip; pos < count; pos += 32)
        {
            const int n = std::min (32, count - pos);
            p.bypassL = byL.data(); p.bypassR = byR.data();
            const bool b = engine.render (0, p, tl.data(), tr.data(), n);
            bypassUsed = bypassUsed || b;
            const float* sl = bypassBus ? (b ? byL.data() : nullptr) : tl.data();
            const float* sr = bypassBus ? (b ? byR.data() : nullptr) : tr.data();
            for (int i = 0; i < n; ++i)
                if (pos + i >= 0)
                {
                    l[(size_t) (pos + i)] = sl != nullptr ? sl[i] : 0.0f;
                    r[(size_t) (pos + i)] = sr != nullptr ? sr[i] : 0.0f;
                }
        }
    }
};

void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * pi / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
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
}

// Magnitude spectrum (4-term Blackman-Harris, ~-92 dB sidelobes), normalised to the largest bin.
std::vector<double> spectrum (const std::vector<float>& x)
{
    std::vector<std::complex<double>> a ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
    {
        const double t = 2.0 * pi * i / fftSize;
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        a[(size_t) i] = x[(size_t) i] * w;
    }
    fft (a);
    std::vector<double> m ((size_t) fftSize / 2);
    double mx = 1e-30;
    for (size_t i = 0; i < m.size(); ++i) { m[i] = std::abs (a[i]); mx = std::max (mx, m[i]); }
    for (auto& v : m) v /= mx;
    return m;
}

inline double binHz() { return fs / fftSize; }
inline double db (double v) { return 20.0 * std::log10 (std::max (v, 1e-12)); }

// Largest component (dB re. peak) in 20 Hz–20 kHz that is not near a multiple of f0.
double worstNonHarmonic (const std::vector<double>& m, double f0, int guard = 6)
{
    double worst = 0.0;
    for (size_t i = 0; i < m.size(); ++i)
    {
        const double f = (double) i * binHz();
        if (f < 20.0 || f > 20000.0) continue;
        const double h = std::round (f / f0);
        if (h >= 1.0 && std::fabs (f - h * f0) <= guard * binHz()) continue;
        worst = std::max (worst, m[i]);
    }
    return db (worst);
}

double level (const std::vector<double>& m, double f)
{
    const auto c = (size_t) std::lround (f / binHz());
    double v = 0.0;
    for (size_t i = c - 3; i <= c + 3; ++i) v = std::max (v, m[i]);
    return db (v);
}

double binAligned (double hz) { return std::round (hz / binHz()) * binHz(); }

double rms (const std::vector<float>& x)
{
    double e = 0.0;
    for (float v : x) e += (double) v * v;
    return std::sqrt (e / (double) x.size());
}
} // namespace

TEST_CASE ("Analog: aliasing below -80 dB at 4x oversampling", "[analog]")
{
    struct Case { int wave; float pw; const char* name; };
    const Case cases[] { { AnalogEngine::Saw, 50.0f, "Saw" }, { AnalogEngine::Square, 50.0f, "Square" },
                         { AnalogEngine::Pulse, 25.0f, "Pulse" }, { AnalogEngine::Triangle, 50.0f, "Triangle" },
                         { AnalogEngine::SuperSaw, 50.0f, "SuperSaw" }, { AnalogEngine::Half, 50.0f, "Half" },
                         { AnalogEngine::Narrow, 50.0f, "Narrow" }, { AnalogEngine::Sine, 50.0f, "Sine" } };
    for (const auto& c : cases)
        for (double hz : { 440.0, 1760.0, 4186.0, 7040.0 })
        {
            AnalogRig rig (4);
            rig.set (lp::anOscWave[0], (float) c.wave);
            rig.set (lp::anOscPw[0], c.pw);
            rig.set (lp::anOscSsDetune[0], 0.0f);   // SuperSaw: 7 phase-randomised saws at one pitch
            const double f0 = binAligned (hz);
            rig.note (f0);
            std::vector<float> l, r;
            rig.render (l, r, fftSize);
            const double worst = worstNonHarmonic (spectrum (l), f0);
            INFO (c.name << " @ " << f0 << " Hz: " << worst << " dB");
            CHECK (worst < -80.0);
        }

    // Sub square (−1 oct) through the same path
    AnalogRig rig (4);
    rig.set (lp::anOscOn[0], 0.0f);
    rig.set (lp::anSubOn, 1.0f);
    rig.set (lp::anSubLevel, 1.0f);
    const double f0 = binAligned (3000.0);
    rig.note (2.0 * f0);
    std::vector<float> l, r;
    rig.render (l, r, fftSize);
    const auto m = spectrum (l);
    CHECK (level (m, f0) > -1.0);
    CHECK (worstNonHarmonic (m, f0) < -80.0);
}

TEST_CASE ("Analog: pitch, octave/semitone and wave mod", "[analog]")
{
    AnalogRig rig (2);
    rig.set (lp::anOscWave[0], AnalogEngine::Sine);
    rig.set (lp::anOscOctave[0], 1.0f);
    rig.set (lp::anOscSemi[0], 7.0f);
    const double base = binAligned (200.0);
    rig.note (base);
    std::vector<float> l, r;
    rig.render (l, r, fftSize);
    auto m = spectrum (l);
    const double f = base * 2.0 * std::exp2 (7.0 / 12.0);
    CHECK (level (m, f) > -0.5);
    CHECK (worstNonHarmonic (m, f) < -90.0);   // pure sine

    // Saw Wave Mod 100% = octave-up saw: no odd harmonics of the played note
    AnalogRig saw (2);
    saw.set (lp::anOscWaveMod[0], 1.0f);
    const double f0 = binAligned (500.0);
    saw.note (f0);
    saw.render (l, r, fftSize);
    m = spectrum (l);
    CHECK (level (m, f0) < -80.0);
    CHECK (level (m, 2.0 * f0) > -1.0);

    // Every wave and mod setting stays finite and bounded
    for (int w = 0; w < 8; ++w)
        for (float wm : { 0.0f, 0.5f, 1.0f })
        {
            AnalogRig a (2);
            a.set (lp::anOscWave[0], (float) w);
            a.set (lp::anOscWaveMod[0], wm);
            a.note (330.0);
            a.render (l, r, 4096, 0);
            float peak = 0.0f;
            for (float v : l) { REQUIRE (std::isfinite (v)); peak = std::max (peak, std::fabs (v)); }
            CHECK (peak < 2.5f);
            CHECK (peak > 0.1f);
        }
}

TEST_CASE ("Analog: hard sync 2->1, FM, ring mod", "[analog]")
{
    std::vector<float> l, r;
    const double f0 = binAligned (300.0);

    SECTION ("hard sync locks OSC 1 to OSC 2 period")
    {
        AnalogRig rig (4);
        rig.set (lp::anOscOn[1], 1.0f);
        rig.set (lp::anOscLevel[1], 0.0f);         // OSC 2 inaudible, still the sync master
        rig.set (lp::anOscSemi[0], 7.3f);          // slave at a non-harmonic ratio
        rig.set (lp::anOscFine[0], 31.0f);
        rig.note (f0);
        rig.render (l, r, fftSize);
        auto m = spectrum (l);
        CHECK (worstNonHarmonic (m, f0) > -40.0);   // without sync: inharmonic
        rig.set (lp::anSync, 1.0f);
        rig.render (l, r, fftSize);
        m = spectrum (l);
        INFO ("sync non-harmonic " << worstNonHarmonic (m, f0));
        CHECK (worstNonHarmonic (m, f0) < -70.0);
    }

    SECTION ("FM 3->1 and 2->1 add sidebands, stay harmonic for integer ratios")
    {
        for (int src : { 2, 1 })
        {
            AnalogRig rig (4);
            rig.set (lp::anOscWave[0], AnalogEngine::Sine);
            rig.set (lp::anOscOn[(size_t) src], 1.0f);
            rig.set (lp::anOscWave[(size_t) src], AnalogEngine::Sine);
            rig.set (lp::anOscLevel[(size_t) src], 0.0f);
            rig.set (lp::anOscOctave[(size_t) src], 1.0f);   // modulator at 2 f
            rig.note (f0);
            rig.render (l, r, fftSize);
            auto m = spectrum (l);
            CHECK (level (m, 3.0 * f0) < -90.0);
            rig.set (src == 2 ? lp::anFm3to1 : lp::anFm2to1, 0.5f);   // deviation 2x carrier: through-zero
            rig.render (l, r, fftSize);
            m = spectrum (l);
            CHECK (level (m, 3.0 * f0) > -30.0);
            CHECK (worstNonHarmonic (m, f0) < -70.0);
            for (float v : l) REQUIRE (std::isfinite (v));
        }
    }

    SECTION ("ring mod 1x2: sum and difference, carrier removed")
    {
        AnalogRig rig (2);
        rig.set (lp::anOscWave[0], AnalogEngine::Sine);
        rig.set (lp::anOscOn[1], 1.0f);
        rig.set (lp::anOscWave[1], AnalogEngine::Sine);
        rig.set (lp::anOscLevel[1], 0.0f);
        rig.set (lp::anOscSemi[1], 7.0f);
        rig.set (lp::anRing, 1.0f);
        rig.note (f0);
        rig.render (l, r, fftSize);
        const auto m = spectrum (l);
        const double f2 = f0 * std::exp2 (7.0 / 12.0);
        CHECK (level (m, f2 + f0) > -8.0);
        CHECK (level (m, f2 - f0) > -8.0);
        CHECK (level (m, f0) < -60.0);
    }
}

TEST_CASE ("Analog: sub, noise, mixer and routing", "[analog]")
{
    std::vector<float> l, r;

    SECTION ("sub sine -2 oct follows OSC 1 pitch")
    {
        AnalogRig rig (2);
        rig.set (lp::anOscOn[0], 0.0f);
        rig.set (lp::anOscOctave[0], 1.0f);
        rig.set (lp::anSubOn, 1.0f);
        rig.set (lp::anSubWave, 0.0f);
        rig.set (lp::anSubOctave, 1.0f);
        const double f0 = binAligned (400.0);
        rig.note (f0);
        rig.render (l, r, fftSize);
        const auto m = spectrum (l);
        CHECK (level (m, f0 * 0.5) > -0.5);
        CHECK (worstNonHarmonic (m, f0 * 0.5) < -90.0);
    }

    SECTION ("noise: white/pink, colour, width")
    {
        const auto hfRatio = [&] (AnalogRig& rig)
        {
            rig.note (440.0);
            rig.render (l, r, fftSize, 0);
            const auto m = spectrum (l);
            double lo = 0.0, hi = 0.0;
            for (size_t i = 0; i < m.size(); ++i)
            {
                const double f = (double) i * binHz();
                if (f > 100.0 && f < 400.0) lo += m[i] * m[i];
                if (f > 8000.0 && f < 16000.0) hi += m[i] * m[i];
            }
            return 10.0 * std::log10 ((hi / 7900.0) / (lo / 300.0));   // per-Hz density ratio, dB
        };
        AnalogRig white (1);
        white.set (lp::anOscOn[0], 0.0f);
        white.set (lp::anNoiseLevel, 1.0f);
        const double w = hfRatio (white);
        CHECK (std::fabs (w) < 3.0);
        CHECK (rms (l) > 0.1);
        for (size_t i = 0; i < l.size(); ++i) REQUIRE (l[i] == r[i]);   // width 0 = mono

        AnalogRig pink (1);
        pink.set (lp::anOscOn[0], 0.0f);
        pink.set (lp::anNoiseLevel, 1.0f);
        pink.set (lp::anNoiseType, 1.0f);
        CHECK (hfRatio (pink) < w - 12.0);

        AnalogRig dark (1);
        dark.set (lp::anOscOn[0], 0.0f);
        dark.set (lp::anNoiseLevel, 1.0f);
        dark.set (lp::anNoiseColor, -1.0f);
        CHECK (hfRatio (dark) < w - 12.0);

        AnalogRig wide (1);
        wide.set (lp::anOscOn[0], 0.0f);
        wide.set (lp::anNoiseLevel, 1.0f);
        wide.set (lp::anNoiseWidth, 1.0f);
        wide.note (440.0);
        wide.render (l, r, 48000, 0);
        double c = 0.0;
        for (size_t i = 0; i < l.size(); ++i) c += (double) l[i] * r[i];
        CHECK (std::fabs (c / (rms (l) * rms (r) * (double) l.size())) < 0.05);
    }

    SECTION ("level 0 and all-off silence")
    {
        AnalogRig rig (2);
        rig.set (lp::anOscOn[1], 1.0f);
        rig.set (lp::anOscWave[1], AnalogEngine::Sine);
        rig.set (lp::anOscWave[0], AnalogEngine::Sine);
        rig.set (lp::anOscOctave[1], 1.0f);
        const double f0 = binAligned (300.0);
        rig.note (f0);
        rig.set (lp::anOscLevel[0], 0.0f);
        rig.render (l, r, fftSize);
        auto m = spectrum (l);
        CHECK (level (m, f0) < -90.0);
        CHECK (level (m, 2.0 * f0) > -3.0);
        rig.set (lp::anOscLevel[0], 0.8f);
        rig.set (lp::anOscLevel[1], 0.0f);
        rig.render (l, r, fftSize);
        m = spectrum (l);
        CHECK (level (m, 2.0 * f0) < -90.0);

        AnalogRig off (2);
        off.set (lp::anOscOn[0], 0.0f);
        off.note (440.0);
        off.render (l, r, 4096, 0);
        CHECK (rms (l) == 0.0);
        CHECK (! off.bypassUsed);
    }

    SECTION ("Filter/Bypass routing per source")
    {
        AnalogRig rig (2);
        rig.set (lp::anOscRoute[0], 1.0f);
        rig.note (440.0);
        rig.render (l, r, 9600);
        CHECK (rms (l) < 1.0e-6);
        rig.render (l, r, 9600, 0, true);
        CHECK (rig.bypassUsed);
        CHECK (rms (l) > 0.2);

        AnalogRig sub (2);
        sub.set (lp::anSubOn, 1.0f);
        sub.set (lp::anSubRoute, 1.0f);
        sub.set (lp::anNoiseLevel, 0.5f);
        sub.set (lp::anNoiseRoute, 1.0f);
        sub.note (440.0);
        sub.render (l, r, 9600, 4800, true);
        CHECK (rms (l) > 0.2);
    }
}

TEST_CASE ("Analog: unison and drift through the synth", "[analog]")
{
    test::Rig rig;   // layer 1 defaults to the Analog engine
    rig.noteOn (60);
    rig.run ({}, 40);
    float peak = 0.0f;
    for (int i = 0; i < rig.buffer.getNumSamples(); ++i)
        peak = std::max (peak, std::fabs (rig.buffer.getSample (0, i)));
    CHECK (peak > 0.05f);

    AnalogRig a (2);
    a.p.unison = 16;
    a.p.detuneCents = 50.0f;
    a.p.spread = 1.0f;
    a.set (lp::anOscWave[0], AnalogEngine::SuperSaw);
    std::array<float, chroma::maxUnison + 1> drift {};
    for (size_t i = 0; i < drift.size(); ++i) drift[i] = 0.1f * (float) i / 16.0f;
    a.p.driftSemis = drift.data();
    a.note (220.0);
    std::vector<float> l, r;
    a.render (l, r, 9600);
    for (float v : l) REQUIRE (std::isfinite (v));
    CHECK (rms (l) > 0.1);
    CHECK (rms (r) > 0.1);
    double diff = 0.0;
    for (size_t i = 0; i < l.size(); ++i) diff += std::fabs (l[i] - r[i]);
    CHECK (diff / (double) l.size() > 0.01);   // stereo spread
}
