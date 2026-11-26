#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "engines/pd/PdEngine.h"
#include "TestHelpers.h"
#include <chrono>
#include <cmath>
#include <random>
#include <vector>

using chroma::PdEngine;
namespace lp = chroma::lp;

namespace
{
constexpr double fs = 48000.0;
constexpr double pi = 3.14159265358979323846;

struct PdRig
{
    PdEngine engine;
    chroma::pd::PhaseCurve curve;
    std::array<float, lp::count> base {};
    chroma::EngineVoiceParams p;

    explicit PdRig (int oversampling = 2)
    {
        for (int i = 0; i < lp::count; ++i)
            base[(size_t) i] = lp::descs[(size_t) i].def;
        engine.prepare (fs, 32, 1);
        engine.setOversampling (oversampling);
        engine.setCustomCurve (&curve);
        p.base = base.data();
        set (lp::pdOutput, 1.0f);
        set (lp::pdLineLevel[0], 1.0f);
    }
    void set (int id, float v) { base[(size_t) id] = v; }
    void note (float pitch, float vel = 1.0f)
    {
        p.pitchStart = p.pitchEnd = pitch;
        engine.noteOn (0, { (int) std::lround (pitch), vel, 0, true });
    }
    void hz (double f, float vel = 1.0f) { note ((float) (69.0 + 12.0 * std::log2 (f / 440.0)), vel); }
    std::vector<float> render (int count, int skip = 4800, std::vector<float>* right = nullptr)
    {
        std::vector<float> l, r;
        float bl[32], br[32];
        for (int done = 0; done < skip + count; done += 32)
        {
            engine.render (0, p, bl, br, 32);
            for (int i = 0; i < 32; ++i)
                if (done + i >= skip && (int) l.size() < count) { l.push_back (bl[i]); r.push_back (br[i]); }
        }
        if (right != nullptr) *right = r;
        return l;
    }
};

double mag (const std::vector<float>& x, double f)
{
    double re = 0.0, im = 0.0, wsum = 0.0;
    const auto n = x.size();
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) (n - 1));
        re += w * x[i] * std::cos (2.0 * pi * f * (double) i / fs);
        im -= w * x[i] * std::sin (2.0 * pi * f * (double) i / fs);
        wsum += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / wsum;
}

double rms (const std::vector<float>& x)
{
    double s = 0.0;
    for (float v : x) s += (double) v * v;
    return std::sqrt (s / (double) std::max<size_t> (1, x.size()));
}

double db (double a) { return 20.0 * std::log10 (std::max (a, 1.0e-12)); }

// Amplitude-weighted mean harmonic number (brightness).
double centroid (const std::vector<float>& x, double f0, int harmonics = 24)
{
    double s = 0.0, w = 0.0;
    for (int k = 1; k <= harmonics && k * f0 < 20000.0; ++k)
    {
        const double m = mag (x, f0 * k);
        s += m * k;
        w += m;
    }
    return w > 0.0 ? s / w : 0.0;
}

double maxStep (const std::vector<float>& x)
{
    double m = 0.0;
    for (size_t i = 1; i < x.size(); ++i) m = std::max (m, (double) std::abs (x[i] - x[i - 1]));
    return m;
}

PdEngine::Ctl ctl (float amount)
{
    PdEngine::Ctl c;
    c.amount = amount;
    return c;
}
} // namespace

TEST_CASE ("PD: phase map closes the cycle for every wave, shape and setting", "[pd]")
{
    chroma::pd::PhaseCurve curve;
    curve.setPoints ({ { 0.3f, 0.7f }, { 0.6f, 0.2f } }, chroma::pd::PhaseCurve::Cubic);
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> u01 (0.0f, 1.0f), u11 (-1.0f, 1.0f);
    for (int w = 0; w < 8; ++w)
        for (int s = 0; s < PdEngine::numShapes; ++s)
            for (int trial = 0; trial < 6; ++trial)
            {
                PdEngine::Ctl c;
                c.amount = u01 (rng); c.morph = u01 (rng); c.curveExp = std::exp2 (3.0f * u11 (rng));
                c.symmetry = u01 (rng); c.asymmetry = u11 (rng); c.offset = u11 (rng);
                c.warpExp = std::exp2 (2.0f * u11 (rng)); c.window = u01 (rng); c.mix = u01 (rng);
                CHECK (PdEngine::distortedPhase (w, s, c, 0.0f, &curve) == Catch::Approx (0.0f).margin (1e-4));
                CHECK (PdEngine::distortedPhase (w, s, c, 0.99999f, &curve) == Catch::Approx (1.0f).margin (2e-3));
                // output continuous across the cycle boundary
                const float a = PdEngine::lineValue (w, s, c, 0.0f, &curve);
                const float b = PdEngine::lineValue (w, s, c, 0.99999f, &curve);
                CHECK (std::abs (a - b) < 0.02f);
                // and inside the cycle: no jumps (the largest step shrinks with denser sampling)
                float dbgAt = 0.0f;
                const auto worstStep = [&] (int n)
                {
                    float prev = a, worst = 0.0f;
                    for (int i = 1; i <= n; ++i)
                    {
                        const float v = PdEngine::lineValue (w, s, c, (float) ((double) i / n * 0.99999), &curve);
                        if (! std::isfinite (v)) return 1.0e9f;
                        if (std::abs (v - prev) > worst) { worst = std::abs (v - prev); dbgAt = (float) ((double) i / n * 0.99999); }
                        prev = v;
                    }
                    return worst;
                };
                const float w1 = worstStep (4000), w2 = worstStep (64000);
                INFO ("w=" << w << " s=" << s << " a=" << c.amount << " m=" << c.morph << " ce=" << c.curveExp << " sym=" << c.symmetry << " asym=" << c.asymmetry << " off=" << c.offset << " we=" << c.warpExp << " at=" << dbgAt << " w1=" << w1 << " w2=" << w2);
                CHECK ((w1 < 0.2f || w2 < 0.25f * w1));
            }
}

TEST_CASE ("PD: Amount 0 is a pure sine for every non-Reso wave", "[pd]")
{
    for (int w = 0; w <= PdEngine::SawPulse; ++w)
    {
        PdRig r;
        r.set (lp::pdLineWave[0], (float) w);
        r.set (lp::pdAmount[0], 0.0f);
        r.hz (440.0);
        const auto x = r.render (8192);
        const double f = mag (x, 440.0);
        CHECK (f > 0.5);
        for (int k = 2; k <= 8; ++k)
            CHECK (db (mag (x, 440.0 * k)) - db (f) < -80.0);
    }
}

TEST_CASE ("PD: Amount sweeps brightness; waves have their spectra", "[pd]")
{
    const double f0 = 220.0;
    auto spectrum = [&] (int wave, float amount, float sym = 0.0f)
    {
        PdRig r (4);
        r.set (lp::pdLineWave[0], (float) wave);
        r.set (lp::pdAmount[0], amount);
        r.set (lp::pdSymmetry[0], sym);
        r.hz (f0);
        return r.render (16384);
    };
    // Saw: brightness grows with Amount; full = all harmonics, ~1/n-like decay
    double last = 0.0;
    for (float a : { 0.0f, 0.3f, 0.6f, 0.9f, 1.0f })
    {
        const double c = centroid (spectrum (PdEngine::Saw, a), f0);
        CHECK (c > last);
        last = c;
    }
    const auto saw = spectrum (PdEngine::Saw, 1.0f);
    for (int k = 2; k <= 6; ++k)
    {
        CHECK (db (mag (saw, f0 * k)) - db (mag (saw, f0)) > -20.0);
        CHECK (mag (saw, f0 * k) < mag (saw, f0 * (k - 1)) * 1.05);
    }
    // Square: half-wave symmetric map → odd harmonics only
    const auto sq = spectrum (PdEngine::Square, 1.0f);
    CHECK (db (mag (sq, 3 * f0)) - db (mag (sq, f0)) > -20.0);
    for (int k : { 2, 4, 6 })
        CHECK (db (mag (sq, k * f0)) - db (mag (sq, f0)) < -60.0);
    // Symmetry 50% on Saw → back to (near) sine; 100% = mirrored saw (same magnitudes)
    const auto mid = spectrum (PdEngine::Saw, 1.0f, 0.5f);
    CHECK (db (mag (mid, 2 * f0)) - db (mag (mid, f0)) < -60.0);
    const auto mir = spectrum (PdEngine::Saw, 1.0f, 1.0f);
    for (int k = 1; k <= 5; ++k)
        CHECK (mag (mir, k * f0) == Catch::Approx (mag (saw, k * f0)).epsilon (0.05).margin (1e-3));
    // Every wave distinct and bounded at full Amount
    std::vector<std::vector<float>> all;
    for (int w = 0; w < 8; ++w)
    {
        all.push_back (spectrum (w, 1.0f));
        double peak = 0.0;
        for (float v : all.back()) peak = std::max (peak, (double) std::abs (v));
        CHECK (peak < 2.5);
        CHECK (rms (all.back()) > 0.1);
    }
    for (size_t a = 0; a < all.size(); ++a)
        for (size_t b = a + 1; b < all.size(); ++b)
        {
            double d = 0.0;
            for (int k = 1; k <= 8; ++k) d += std::abs (mag (all[a], k * f0) - mag (all[b], k * f0));
            CHECK (d > 0.02);
        }
}

TEST_CASE ("PD: Reso resonance follows Amount and is band-limited", "[pd]")
{
    const double f0 = 110.0;
    auto peakHarmonic = [&] (int wave, float amount)
    {
        PdRig r (2);
        r.set (lp::pdLineWave[0], (float) wave);
        r.set (lp::pdAmount[0], amount);
        r.hz (f0);
        const auto x = r.render (16384);
        int best = 1;
        double bm = 0.0;
        for (int k = 1; k <= 24; ++k)
            if (const double m = mag (x, k * f0); m > bm) { bm = m; best = k; }
        return best;
    };
    for (int w = PdEngine::Reso1; w <= PdEngine::Reso3; ++w)
    {
        const int k1 = peakHarmonic (w, 0.2f), k2 = peakHarmonic (w, 0.6f);
        CHECK (k1 >= 3);
        CHECK (k1 <= 5);
        CHECK (k2 >= 9);
        CHECK (k2 <= 11);
    }
    // At a high note the resonance is capped below Nyquist
    PdEngine::Ctl c = ctl (1.0f);
    c.resoMax = 3.0f;
    chroma::pd::PhaseCurve none;
    PdEngine::Map m;
    PdEngine::prepareMap (m, PdEngine::Reso1, PdEngine::Linear, c, &none);
    CHECK (m.reso == Catch::Approx (3.0f));
}

TEST_CASE ("PD: pitch, octave, coarse, fine and line pitch", "[pd]")
{
    PdRig r;
    r.set (lp::pdAmount[0], 0.0f);
    r.set (lp::pdLineOctave[0], 1.0f);
    r.set (lp::pdLineCoarse[0], -12.0f);
    r.set (lp::pdLineFine[0], 0.0f);
    r.set (lp::pdLinePitch[0], 7.0f);
    r.hz (220.0);
    const auto x = r.render (16384);
    const double f = 220.0 * std::pow (2.0, 7.0 / 12.0);
    CHECK (db (mag (x, f)) > -10.0);
    CHECK (mag (x, 220.0) < 0.01);
    r.set (lp::pdLinePitch[0], 0.0f);
    r.set (lp::pdLineFine[0], 50.0f);
    r.hz (440.0);
    const auto y = r.render (16384);
    CHECK (mag (y, 440.0 * std::pow (2.0, 0.5 / 12.0)) > 4.0 * mag (y, 440.0));
}

TEST_CASE ("PD: modulation keeps the phase continuous (no clicks)", "[pd]")
{
    // Every PD control jumps between two settings each block; the ramped map keeps the output as smooth
    // as a steady render of the brighter setting.
    auto run = [] (bool modulate)
    {
        PdRig r (2);
        r.set (lp::pdLineWave[0], (float) PdEngine::Saw);
        r.set (lp::pdShape[0], (float) PdEngine::Smooth);
        r.hz (220.0);
        std::vector<float> out;
        float bl[32], br[32];
        for (int b = 0; b < 600; ++b)
        {
            const bool hi = ! modulate || (b & 1);
            r.set (lp::pdAmount[0], hi ? 0.7f : 0.3f);
            r.set (lp::pdMorph[0], hi ? 0.6f : 0.1f);
            r.set (lp::pdSymmetry[0], hi ? 0.2f : 0.0f);
            r.set (lp::pdOffset[0], hi ? 0.3f : -0.3f);
            r.set (lp::pdWarp[0], hi ? 0.3f : -0.3f);
            r.set (lp::pdAsymmetry[0], hi ? 0.2f : -0.2f);
            r.set (lp::pdCurve[0], hi ? 0.2f : -0.2f);
            r.set (lp::pdLinePitch[0], hi ? 0.5f : 0.0f);
            r.engine.render (0, r.p, bl, br, 32);
            if (b > 100) out.insert (out.end(), bl, bl + 32);
        }
        return out;
    };
    const auto steady = run (false), mod = run (true);
    CHECK (maxStep (mod) < 1.5 * maxStep (steady));

    // the oscillator phase never resets on modulation: a pure sine with amount toggling 0 ↔ tiny
    PdRig r;
    r.set (lp::pdAmount[0], 0.0f);
    r.hz (440.0);
    const auto a = r.render (4800, 0);
    PdRig q;
    q.set (lp::pdAmount[0], 0.0f);
    q.hz (440.0);
    std::vector<float> b;
    float bl[32], br[32];
    for (int k = 0; k < 150; ++k)
    {
        q.set (lp::pdMix[0], (k & 1) ? 1.0f : 0.5f);   // mix of sine with itself: identical signal
        q.engine.render (0, q.p, bl, br, 32);
        b.insert (b.end(), bl, bl + 32);
    }
    for (size_t i = 0; i < a.size(); ++i)
        REQUIRE (b[i] == Catch::Approx (a[i]).margin (1e-4));
}

TEST_CASE ("PD: oversampling and band-limited knee keep aliasing low", "[pd]")
{
    auto inharm = [] (int os)
    {
        PdRig r (os);
        r.set (lp::pdLineWave[0], (float) PdEngine::Saw);
        r.set (lp::pdAmount[0], 1.0f);
        const double f0 = 1760.0;
        r.hz (f0);
        const auto x = r.render (16384);
        double h = 0.0, a = 0.0;
        for (int k = 1; k * f0 < 20000.0; ++k) h = std::max (h, mag (x, f0 * k));
        for (double f = 100.0; f < 20000.0; f += 37.0)
        {
            const double k = std::round (f / f0);
            if (std::abs (f - k * f0) < 60.0) continue;
            a = std::max (a, mag (x, f));
        }
        return db (a) - db (h);
    };
    const double a1 = inharm (1), a4 = inharm (4);
    CHECK (a4 < a1);
    CHECK (a4 < -70.0);
}

TEST_CASE ("PD: Shape, Morph, Curve and Custom curve", "[pd]")
{
    chroma::pd::PhaseCurve curve;
    curve.setPoints ({ { 0.25f, 0.6f }, { 0.75f, 0.8f } }, chroma::pd::PhaseCurve::Linear);
    REQUIRE (curve.points().size() == 4);
    CHECK (curve.evaluate (0.25f) == Catch::Approx (0.6f).margin (1e-3));
    CHECK (curve.evaluate (0.5f) == Catch::Approx (0.7f).margin (1e-3));
    CHECK (curve.evaluate (1.0f) == Catch::Approx (1.0f));

    auto c = ctl (1.0f);
    // Custom at full amount = the curve itself
    for (float x : { 0.1f, 0.4f, 0.9f })
        CHECK (PdEngine::distortedPhase (PdEngine::Saw, PdEngine::Custom, c, x, &curve)
               == Catch::Approx (curve.evaluate (x)).margin (1e-4));
    // Morph ends equal shape A / next shape
    for (int s = 0; s < PdEngine::numShapes; ++s)
        for (float x : { 0.05f, 0.3f, 0.7f })
        {
            auto m0 = c, m1 = c;
            m0.morph = 0.0f;
            m1.morph = 1.0f;
            auto nx = c;
            nx.morph = 0.0f;
            CHECK (PdEngine::distortedPhase (PdEngine::Saw, s, m1, x, &curve)
                   == Catch::Approx (PdEngine::distortedPhase (PdEngine::Saw, (s + 1) % PdEngine::numShapes, nx, x, &curve)).margin (1e-5));
            auto h = c;
            h.morph = 0.5f;
            const float a = PdEngine::distortedPhase (PdEngine::Saw, s, m0, x, &curve);
            const float b = PdEngine::distortedPhase (PdEngine::Saw, s, m1, x, &curve);
            CHECK (PdEngine::distortedPhase (PdEngine::Saw, s, h, x, &curve) == Catch::Approx (0.5f * (a + b)).margin (1e-5));
        }
    // Shapes differ from Linear; Curve bends the segments
    for (int s = PdEngine::Smooth; s <= PdEngine::Steps; ++s)
        CHECK (std::abs (PdEngine::distortedPhase (PdEngine::Saw, s, c, 0.3f, &curve)
                         - PdEngine::distortedPhase (PdEngine::Saw, PdEngine::Linear, c, 0.3f, &curve)) > 1e-3f);
    auto cv = c;
    cv.curveExp = 2.0f;
    CHECK (PdEngine::distortedPhase (PdEngine::Saw, PdEngine::Linear, cv, 0.3f, &curve)
           < PdEngine::distortedPhase (PdEngine::Saw, PdEngine::Linear, c, 0.3f, &curve));
    // Smooth / Cubic interpolation pass through the points
    curve.setPoints ({ { 0.25f, 0.6f }, { 0.75f, 0.8f } }, chroma::pd::PhaseCurve::Cubic);
    CHECK (curve.evaluate (0.75f) == Catch::Approx (0.8f).margin (2e-3));
    curve.setPoints ({ { 0.5f, 0.2f } }, chroma::pd::PhaseCurve::Smooth);
    CHECK (curve.evaluate (0.5f) == Catch::Approx (0.2f).margin (2e-3));
    CHECK (curve.evaluate (0.25f) == Catch::Approx (0.1f).margin (2e-3));

    // Engine: Custom curve changes the sound, Window and Mix behave
    PdRig r;
    r.set (lp::pdShape[0], (float) PdEngine::Custom);
    r.set (lp::pdAmount[0], 1.0f);
    r.hz (220.0);
    const auto lin = r.render (8192);
    r.curve.setPoints ({ { 0.1f, 0.5f } }, chroma::pd::PhaseCurve::Linear);
    r.hz (220.0);
    const auto cus = r.render (8192);
    CHECK (mag (cus, 440.0) > 0.05);
    CHECK (mag (lin, 440.0) < 0.01);   // identity custom curve = sine
    r.set (lp::pdMix[0], 0.0f);
    r.hz (220.0);
    const auto dry = r.render (8192);
    CHECK (db (mag (dry, 440.0)) - db (mag (dry, 220.0)) < -70.0);
    r.set (lp::pdMix[0], 1.0f);
    r.set (lp::pdWindow[0], 1.0f);
    r.set (lp::pdShape[0], (float) PdEngine::Linear);
    r.set (lp::pdAmount[0], 0.0f);
    r.hz (220.0);
    const auto win = r.render (8192);
    CHECK (mag (win, 440.0) > 0.1);   // sine × raised cosine adds the 2nd harmonic
}

TEST_CASE ("PD: Warp, Asymmetry and Offset reshape the wave", "[pd]")
{
    auto c = ctl (0.0f);
    c.warpExp = 2.0f;   // x / (x + 2(1 − x)) at 0.5 = 1/3
    CHECK (PdEngine::distortedPhase (PdEngine::Saw, 0, c, 0.5f, nullptr) == Catch::Approx (1.0f / 3.0f).margin (1e-4));
    c.warpExp = 1.0f;
    c.asymmetry = 1.0f;   // knee at 0.05
    CHECK (PdEngine::distortedPhase (PdEngine::Saw, 0, c, 0.05f, nullptr) == Catch::Approx (0.5f).margin (1e-4));
    // Offset changes the timbre only with distortion
    auto a = ctl (0.0f), b = ctl (0.0f);
    b.offset = 0.5f;
    std::vector<float> xa, xb;
    for (int i = 0; i < 1000; ++i)
    {
        xa.push_back (PdEngine::lineValue (PdEngine::Saw, 0, a, (float) i / 1000.0f, nullptr));
        xb.push_back (PdEngine::lineValue (PdEngine::Saw, 0, b, (float) i / 1000.0f, nullptr));
    }
    CHECK (std::abs (rms (xa) - rms (xb)) < 1e-3);
    PdRig r;
    r.set (lp::pdAmount[0], 0.8f);
    r.hz (220.0);
    const auto x0 = r.render (8192);
    r.set (lp::pdOffset[0], 0.5f);
    r.hz (220.0);
    const auto x1 = r.render (8192);
    double d = 0.0;
    for (int k = 1; k <= 6; ++k) d += std::abs (mag (x0, k * 220.0) - mag (x1, k * 220.0));
    CHECK (d > 0.05);
}

TEST_CASE ("PD: PD ENV drives the distortion over time", "[pd]")
{
    PdRig r;
    r.set (lp::pdAmount[0], 0.0f);
    r.set (lp::pdEnvAmount[0], 1.0f);
    r.set (lp::pdEnvAttack[0], 0.001f);
    r.set (lp::pdEnvDecay[0], 0.3f);
    r.set (lp::pdEnvSustain[0], 0.0f);
    r.hz (220.0);
    const auto early = r.render (2048, 64);
    const auto late = r.render (4096, 48000);
    CHECK (centroid (early, 220.0) > centroid (late, 220.0) + 0.5);
    CHECK (db (mag (late, 440.0)) - db (mag (late, 220.0)) < -60.0);

    // velocity scaling
    r.set (lp::pdEnvVelocity[0], 1.0f);
    r.hz (220.0, 0.1f);
    const auto soft = r.render (2048, 64);
    CHECK (centroid (soft, 220.0) < centroid (early, 220.0));

    // release: env falls after note off
    r.set (lp::pdEnvVelocity[0], 0.0f);
    r.set (lp::pdEnvSustain[0], 1.0f);
    r.set (lp::pdEnvRelease[0], 0.05f);
    r.hz (220.0);
    const auto held = r.render (4096, 4800);
    r.engine.noteOff (0);
    const auto rel = r.render (4096, 9600);
    CHECK (centroid (held, 220.0) > centroid (rel, 220.0) + 0.5);
}

TEST_CASE ("PD: two lines, Ring and Noise mod", "[pd]")
{
    PdRig r (2);
    r.set (lp::pdAmount[0], 0.0f);
    r.set (lp::pdLineOn[1], 1.0f);
    r.set (lp::pdLineLevel[1], 1.0f);
    r.set (lp::pdAmount[1], 0.0f);
    r.set (lp::pdLineCoarse[1], 7.0f);
    r.hz (200.0);
    const double f2 = 200.0 * std::pow (2.0, 7.0 / 12.0);
    const auto two = r.render (16384);
    CHECK (mag (two, 200.0) > 0.3);
    CHECK (mag (two, f2) > 0.3);

    // Ring: line 2 → sidebands f2 ± f1 (line 1 still audible)
    r.set (lp::pdRing, 1.0f);
    r.hz (200.0);
    const auto ring = r.render (16384);
    CHECK (mag (ring, f2 + 200.0) > 0.2);
    CHECK (mag (ring, f2 - 200.0) > 0.2);
    CHECK (mag (ring, f2) < 0.05);

    // Ring with line 1 at level 0: line 1 still runs as the modulator, but is silent
    r.set (lp::pdLineLevel[0], 0.0f);
    r.hz (200.0);
    const auto ring0 = r.render (16384);
    CHECK (mag (ring0, 200.0) < 0.01);
    CHECK (mag (ring0, f2 + 200.0) > 0.2);

    // Noise mod spreads line 2 into noise
    r.set (lp::pdRing, 0.0f);
    r.set (lp::pdLineLevel[0], 1.0f);
    r.set (lp::pdNoise, 1.0f);
    r.hz (200.0);
    const auto nz = r.render (16384);
    CHECK (mag (nz, f2) < 0.5 * mag (two, f2));
    CHECK (mag (nz, 200.0) > 0.3);                       // line 1 untouched
    CHECK (mag (nz, 1234.0) > 10.0 * mag (two, 1234.0)); // broadband
}

TEST_CASE ("PD: lines off = silence; unison and pan", "[pd]")
{
    PdRig r;
    r.set (lp::pdLineOn[0], 0.0f);
    r.hz (440.0);
    const auto s = r.render (4096);
    CHECK (rms (s) == 0.0);

    r.set (lp::pdLineOn[0], 1.0f);
    r.set (lp::pdLinePan[0], -1.0f);
    r.hz (440.0);
    std::vector<float> right;
    const auto left = r.render (4096, 4800, &right);
    CHECK (rms (left) > 0.3);
    CHECK (rms (right) < 1e-3);

    r.set (lp::pdLinePan[0], 0.0f);
    r.p.unison = 4;
    r.p.detuneCents = 30.0f;
    r.p.spread = 1.0f;
    r.hz (440.0);
    const auto l = r.render (16384, 4800, &right);
    double diff = 0.0;
    for (size_t i = 0; i < l.size(); ++i) diff += std::abs (l[i] - right[i]);
    CHECK (diff / (double) l.size() > 0.01);
    for (float v : l) REQUIRE (std::isfinite (v));
}

TEST_CASE ("PD: plays through the synth when selected as layer engine; curve in state", "[pd]")
{
    test::Rig rig;
    test::setParam (rig.proc, chroma::ParamIDs::layerEngine[0], 4.0f);
    auto& store = rig.synth().layerStore (0);
    store.set (lp::pdAmount[0], 0.9f);
    store.set (lp::pdLineOn[1], 1.0f);
    store.set (lp::pdLineWave[1], (float) PdEngine::Reso2);
    store.pdCurve().setPoints ({ { 0.4f, 0.1f } }, chroma::pd::PhaseCurve::Smooth);
    rig.run ({}, 2);
    rig.noteOn (60);
    double e = 0.0;
    for (int b = 0; b < 40; ++b)
    {
        rig.run();
        for (int i = 0; i < rig.buffer.getNumSamples(); ++i)
        {
            const float v = rig.buffer.getSample (0, i);
            REQUIRE (std::isfinite (v));
            e += (double) v * v;
        }
    }
    CHECK (e > 1.0);

    const auto tree = store.toTree ("L");
    chroma::LayerParamStore other;
    other.fromTree (tree);
    REQUIRE (other.pdCurve().points().size() == 3);
    CHECK (other.pdCurve().points()[1].x == Catch::Approx (0.4f));
    CHECK (other.pdCurve().points()[1].y == Catch::Approx (0.1f));
    CHECK (other.pdCurve().interpolation() == chroma::pd::PhaseCurve::Smooth);
    CHECK (other.get (lp::pdAmount[0]) == Catch::Approx (0.9f));
}

TEST_CASE ("PD: CPU of one voice (2 lines, morph, 2x)", "[.bench]")
{
    PdRig r (2);
    r.set (lp::pdLineOn[1], 1.0f);
    r.set (lp::pdMorph[0], 0.5f);
    r.set (lp::pdMorph[1], 0.5f);
    r.set (lp::pdCurve[0], 0.3f);
    r.set (lp::pdRing, 0.5f);
    r.hz (440.0);
    const int seconds = 5;
    const auto t0 = std::chrono::steady_clock::now();
    r.render ((int) fs * seconds, 0);
    const double el = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    WARN ("PD voice: " << 100.0 * el / seconds << " % of one core @48 kHz");
}
