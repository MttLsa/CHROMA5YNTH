#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "engines/additive/AdditiveEngine.h"
#include "TestHelpers.h"
#include <chrono>
#include <cmath>
#include <vector>

using chroma::AdditiveEngine;
namespace lp = chroma::lp;
namespace add = chroma::add;

namespace
{
constexpr double fs = 48000.0;
constexpr double pi = 3.14159265358979323846;

struct AddRig
{
    AdditiveEngine engine;
    add::CustomSpectrum customA, customB;
    std::array<float, lp::count> base {};
    chroma::EngineVoiceParams p;

    explicit AddRig (int voices = 1)
    {
        for (int i = 0; i < lp::count; ++i)
            base[(size_t) i] = lp::descs[(size_t) i].def;
        engine.setCustomSpectra (&customA, &customB);
        engine.prepare (fs, 32, voices);
        p.base = base.data();
        set (lp::addOutput, 1.0f);
    }
    void set (int id, float v) { base[(size_t) id] = v; }
    void shape (int a, int b = -1) { set (lp::addSpectrumA, (float) a); set (lp::addSpectrumB, (float) (b < 0 ? a : b)); }
    void hz (double f, int voice = 0)
    {
        const float pitch = (float) (69.0 + 12.0 * std::log2 (f / 440.0));
        p.pitchStart = p.pitchEnd = pitch;
        engine.noteOn (voice, { (int) std::lround (pitch), 1.0f, 0, true });
    }
    std::vector<float> render (int count, int skip = 2400, std::vector<float>* right = nullptr)
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
    // Custom A = only the given partials (1-based) at amplitude 1.
    void customOnly (std::initializer_list<int> partials)
    {
        shape (add::Custom);
        for (int k = 0; k < add::maxPartials; ++k)
        {
            customA.set (add::fAmp, k, 0.0f);
            customA.set (add::fRatio, k, (float) (k + 1));
        }
        for (int k : partials) customA.set (add::fAmp, k - 1, 1.0f);
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

double peakFreq (const std::vector<float>& x, double lo, double hi, double step)
{
    double best = lo, bm = -1.0;
    for (double f = lo; f <= hi; f += step)
        if (const double m = mag (x, f); m > bm) { bm = m; best = f; }
    return best;
}

double db (double a) { return 20.0 * std::log10 (std::max (a, 1.0e-12)); }
} // namespace

TEST_CASE ("Additive: fast math", "[additive]")
{
    double maxSin = 0.0, maxLog = 0.0, maxExp = 0.0;
    for (int i = -4000; i <= 4000; ++i)
    {
        const float x = (float) i * 0.0123f;
        maxSin = std::max (maxSin, std::abs ((double) add::sin2pi (x) - std::sin (2.0 * pi * x)));
        const float e = (float) i * 0.01f;
        maxExp = std::max (maxExp, std::abs ((double) add::fastExp2 (e) / std::exp2 ((double) e) - 1.0));
        const float l = 0.001f + (float) (i + 4000) * 0.25f;
        maxLog = std::max (maxLog, std::abs ((double) add::fastLog2 (l) - std::log2 ((double) l)));
    }
    CHECK (maxSin < 1.0e-5);
    CHECK (maxExp < 1.0e-5);
    CHECK (maxLog < 2.0e-4);
}

TEST_CASE ("Additive: factory shapes", "[additive]")
{
    const auto& saw = add::shapeTable (add::Saw);
    const auto& sq = add::shapeTable (add::Square);
    const auto& tri = add::shapeTable (add::Triangle);
    for (int k = 0; k < 16; ++k)
    {
        CHECK (saw.at (add::fAmp, k) == Catch::Approx (1.0 / (k + 1)));
        CHECK (saw.at (add::fRatio, k) == Catch::Approx (k + 1));
        CHECK (sq.at (add::fAmp, k) == Catch::Approx ((k % 2 == 0) ? 1.0 / (k + 1) : 0.0));
    }
    CHECK (tri.at (add::fPolarity, 0) == 1.0f);
    CHECK (tri.at (add::fPolarity, 2) == -1.0f);
    CHECK (tri.at (add::fPolarity, 4) == 1.0f);
    for (int s = 1; s < add::numShapes; ++s)
    {
        float sum = 0.0f;
        for (int k = 0; k < add::maxPartials; ++k)
        {
            REQUIRE (std::isfinite (add::shapeTable (s).at (add::fAmp, k)));
            sum += add::shapeTable (s).at (add::fAmp, k);
        }
        CHECK (sum > 0.5f);
    }
}

TEST_CASE ("Additive: sine and saw spectra", "[additive]")
{
    {
        AddRig r;
        r.shape (add::Sine);
        r.hz (440.0);
        const auto x = r.render (9600);
        CHECK (mag (x, 440.0) == Catch::Approx (AdditiveEngine::outputScale).epsilon (0.01));
        CHECK (db (mag (x, 880.0) / mag (x, 440.0)) < -90.0);
    }
    {
        AddRig r;
        r.shape (add::Saw);
        r.hz (300.0);
        const auto x = r.render (9600);
        const double h1 = mag (x, 300.0);
        for (int k = 2; k <= 10; ++k)
            CHECK (mag (x, 300.0 * k) / h1 == Catch::Approx (1.0 / k).epsilon (0.02));
    }
}

TEST_CASE ("Additive: partial count and Nyquist muting", "[additive]")
{
    {
        AddRig r;
        r.shape (add::Saw);
        r.set (lp::addPartials, 8.0f);
        r.hz (300.0);
        const auto x = r.render (9600);
        CHECK (mag (x, 2400.0) > 0.01);
        CHECK (db (mag (x, 2700.0) / mag (x, 300.0)) < -90.0);
    }
    {
        AddRig r;
        r.shape (add::Flat);
        r.set (lp::addPartials, 256.0f);
        const double f = 1234.5;
        r.hz (f);
        const auto x = r.render (16384);
        // harmonic 35 (43.2 kHz) would alias to 4792.5 Hz
        CHECK (mag (x, fs - 35.0 * f) < 1.0e-4);
        CHECK (mag (x, 17.0 * f) > 0.05);       // 21 kHz: still below the fade
        for (float v : x) REQUIRE (std::isfinite (v));
    }
}

TEST_CASE ("Additive: odd / even, tilt, brightness / slope, balance, density", "[additive]")
{
    const double f = 250.0;
    auto spectrum = [&] (auto&& setup)
    {
        AddRig r;
        r.shape (add::Saw);
        setup (r);
        r.hz (f);
        return r.render (9600);
    };
    {
        const auto x = spectrum ([] (AddRig& r) { r.set (lp::addEven, 0.0f); });
        CHECK (db (mag (x, 2 * f) / mag (x, f)) < -80.0);
        CHECK (mag (x, 3 * f) / mag (x, f) == Catch::Approx (1.0 / 3).epsilon (0.02));
    }
    {
        const auto x = spectrum ([] (AddRig& r) { r.set (lp::addOdd, 0.0f); });
        CHECK (db (mag (x, 3 * f) / mag (x, f)) < -80.0);
        CHECK (mag (x, f) > 0.4);
        CHECK (mag (x, 2 * f) / mag (x, f) == Catch::Approx (0.5).epsilon (0.02));
    }
    {
        const auto x = spectrum ([] (AddRig& r) { r.set (lp::addTilt, -6.0206f); });
        CHECK (mag (x, 2 * f) / mag (x, f) == Catch::Approx (0.25).epsilon (0.02));
        CHECK (mag (x, 4 * f) / mag (x, f) == Catch::Approx (1.0 / 16).epsilon (0.02));
    }
    {
        const auto ref = spectrum ([] (AddRig&) {});
        const auto x = spectrum ([] (AddRig& r) { r.set (lp::addBrightness, 3.0f / 8.0f); r.set (lp::addSlope, 24.0f); });
        CHECK (mag (x, 8 * f) / mag (ref, 8 * f) == Catch::Approx (1.0).epsilon (0.02));
        CHECK (db (mag (x, 16 * f) / mag (ref, 16 * f)) == Catch::Approx (-24.0).margin (0.5));
    }
    {
        const auto ref = spectrum ([] (AddRig&) {});
        const auto x = spectrum ([] (AddRig& r) { r.set (lp::addBalance, 1.0f); r.set (lp::addPartials, 9.0f); });
        CHECK (mag (x, f) < 1.0e-4);                                                  // lowest partial removed
        CHECK (mag (x, 9 * f) / mag (ref, 9 * f) == Catch::Approx (2.0).epsilon (0.02));   // highest doubled
    }
    {
        const auto x = spectrum ([] (AddRig& r) { r.set (lp::addDensity, 0.0f); });
        CHECK (mag (x, f) > 0.4);   // fundamental kept
        int alive = 0;
        for (int k = 2; k <= 32; ++k) alive += mag (x, k * f) > 1.0e-4 ? 1 : 0;
        CHECK (alive == 0);
        const auto h = spectrum ([] (AddRig& r) { r.set (lp::addDensity, 0.5f); });
        alive = 0;
        for (int k = 2; k <= 32; ++k) alive += mag (h, k * f) > 1.0e-4 ? 1 : 0;
        CHECK (alive > 8);
        CHECK (alive < 24);
    }
}

TEST_CASE ("Additive: morph A <-> B and distribution", "[additive]")
{
    const double f = 300.0;
    {
        AddRig r;
        r.shape (add::Sine, add::Square);
        r.set (lp::addMorph, 0.5f);
        r.hz (f);
        const auto x = r.render (9600);
        CHECK (mag (x, f) == Catch::Approx (0.5).epsilon (0.02));
        CHECK (mag (x, 3 * f) == Catch::Approx (0.5 * 0.5 / 3).epsilon (0.03));
        CHECK (mag (x, 2 * f) < 1.0e-4);
    }
    {
        AddRig r;
        r.shape (add::Saw);
        r.set (lp::addDistribution, 2.0f);   // Odd: 1, 3, 5 …
        r.hz (f);
        const auto x = r.render (9600);
        CHECK (mag (x, 3 * f) / mag (x, f) == Catch::Approx (0.5).epsilon (0.02));
        CHECK (mag (x, 2 * f) < 1.0e-4);
    }
    {
        AddRig r;
        r.shape (add::Saw);
        r.set (lp::addDistribution, 3.0f);   // Octaves
        r.hz (f);
        const auto x = r.render (9600);
        CHECK (mag (x, 4 * f) / mag (x, f) == Catch::Approx (1.0 / 3).epsilon (0.02));
        CHECK (mag (x, 3 * f) < 1.0e-4);
    }
}

TEST_CASE ("Additive: per-partial ratio, fixed frequency, detune, fine; inharmonicity, stretch, shift", "[additive]")
{
    const double f = 200.0;
    auto peakOf = [&] (auto&& setup, double lo, double hi)
    {
        AddRig r;
        r.customOnly ({ 10 });
        setup (r);
        r.hz (f);
        return peakFreq (r.render (24000), lo, hi, 0.5);
    };
    CHECK (peakOf ([] (AddRig&) {}, 1900, 2100) == Catch::Approx (2000.0).margin (0.6));
    CHECK (peakOf ([] (AddRig& r) { r.customA.set (add::fRatio, 9, 7.5f); }, 1400, 1600) == Catch::Approx (1500.0).margin (0.6));
    CHECK (peakOf ([] (AddRig& r) { r.customA.set (add::fFreq, 9, 1234.0f); }, 1100, 1400) == Catch::Approx (1234.0).margin (0.6));
    CHECK (peakOf ([] (AddRig& r) { r.customA.set (add::fDetune, 9, 7.0f); }, 1900, 2100) == Catch::Approx (2007.0).margin (0.6));
    CHECK (peakOf ([] (AddRig& r) { r.customA.set (add::fFine, 9, 100.0f); }, 2000, 2200) == Catch::Approx (2000.0 * std::exp2 (1.0 / 12)).margin (0.8));
    CHECK (peakOf ([] (AddRig& r) { r.set (lp::addShift, 50.0f); }, 1900, 2100) == Catch::Approx (2050.0).margin (0.6));
    CHECK (peakOf ([] (AddRig& r) { r.set (lp::addInharm, 1.0f); }, 2000, 2400) == Catch::Approx (2000.0 * std::sqrt (1.0 + 0.002 * 100)).margin (1.0));
    CHECK (peakOf ([] (AddRig& r) { r.set (lp::addStretch, 1.0f); }, 2000, 2600) == Catch::Approx (f * std::pow (10.0, 1.1)).margin (1.0));
    // negative frequency after Shift → muted
    {
        AddRig r;
        r.customOnly ({ 1 });
        r.set (lp::addShift, -500.0f);
        r.hz (f);
        CHECK (rms (r.render (4800)) < 1.0e-6);
    }
}

TEST_CASE ("Additive: mute, solo, polarity, phase", "[additive]")
{
    const double f = 300.0;
    {
        AddRig r;
        r.customOnly ({ 1, 2, 3 });
        r.customA.set (add::fMute, 1, 1.0f);
        r.hz (f);
        const auto x = r.render (9600);
        CHECK (mag (x, 2 * f) < 1.0e-4);
        CHECK (mag (x, 3 * f) > 0.4);
    }
    {
        AddRig r;
        r.customOnly ({ 1, 2, 3 });
        r.customA.set (add::fSolo, 1, 1.0f);
        r.hz (f);
        const auto x = r.render (9600);
        CHECK (mag (x, f) < 1.0e-4);
        CHECK (mag (x, 2 * f) > 0.4);
        CHECK (mag (x, 3 * f) < 1.0e-4);
    }
    {
        // phase 0.25 cycles → cosine start; polarity −1 flips it
        AddRig r;
        r.customOnly ({ 1 });
        r.customA.set (add::fPhase, 0, 0.25f);
        r.hz (f);
        CHECK (r.render (4, 0)[0] == Catch::Approx (0.5).margin (1e-3));
        AddRig q;
        q.customOnly ({ 1 });
        q.customA.set (add::fPhase, 0, 0.25f);
        q.customA.set (add::fPolarity, 0, -1.0f);
        q.hz (f);
        CHECK (q.render (4, 0)[0] == Catch::Approx (-0.5).margin (1e-3));
    }
    {
        // Phase Reset off → random start phase
        AddRig r;
        r.customOnly ({ 1 });
        r.set (lp::addPhaseReset, 0.0f);
        r.hz (f);
        CHECK (std::abs (r.render (4, 0)[0]) > 1.0e-3);
    }
}

TEST_CASE ("Additive: partial ENV scales time per partial", "[additive]")
{
    const double f = 200.0;
    auto ratioAt = [&] (float scaling, double atSec)
    {
        AddRig r;
        r.customOnly ({ 1, 8 });
        r.set (lp::addPenvAmount, 1.0f);
        r.set (lp::addPenvDist, 0.0f);
        r.set (lp::addPenvAttack, 0.0f);
        r.set (lp::addPenvDecay, 0.5f);
        r.set (lp::addPenvSustain, 0.0f);
        r.set (lp::addPenvCurve, 0.0f);   // linear
        r.set (lp::addPenvScaling, scaling);
        r.hz (f);
        const auto x = r.render (2400, (int) (atSec * fs));
        return std::pair { mag (x, f), mag (x, 8 * f) };
    };
    const auto [a1, a8] = ratioAt (0.0f, 0.2);
    CHECK (a8 / a1 == Catch::Approx (1.0).epsilon (0.05));
    const auto [b1, b8] = ratioAt (1.0f, 0.2);
    CHECK (b1 > 0.1);
    CHECK (b8 < 1.0e-3);   // partial 8 decays 8× faster (gone after 62.5 ms)
    CHECK (AdditiveEngine::partialEnvHeld (0.25f, 0.0f, 0.5f, 0.0f, 0) == Catch::Approx (0.5).margin (1e-3));
    CHECK (AdditiveEngine::partialEnvHeld (2.0f, 0.0f, 0.5f, 0.3f, 1) == Catch::Approx (0.3f));
}

TEST_CASE ("Additive: spectral ENV moves tilt and morph", "[additive]")
{
    const double f = 250.0;
    AddRig r;
    r.shape (add::Saw);
    r.set (lp::addSenvAmount, 1.0f);
    r.set (lp::addSenvTilt, -24.0f);
    r.set (lp::addSenvAttack, 0.0f);
    r.set (lp::addSenvDecay, 0.3f);
    r.set (lp::addSenvSustain, 0.0f);
    r.hz (f);
    const auto early = r.render (1024, 64);
    const auto late = r.render (2400, (int) (0.6 * fs));
    CHECK (mag (early, 4 * f) / mag (early, f) < 0.02);
    CHECK (mag (late, 4 * f) / mag (late, f) == Catch::Approx (0.25).epsilon (0.05));

    AddRig m;
    m.shape (add::Sine, add::Square);
    m.set (lp::addSenvAmount, 1.0f);
    m.set (lp::addSenvTilt, 0.0f);
    m.set (lp::addSenvPosition, 1.0f);
    m.set (lp::addSenvAttack, 0.0f);
    m.set (lp::addSenvDecay, 0.2f);
    m.set (lp::addSenvSustain, 0.5f);
    m.hz (f);
    const auto x = m.render (4800, (int) (0.5 * fs));
    CHECK (mag (x, 3 * f) == Catch::Approx (0.5 * 0.5 / 3).epsilon (0.03));
}

TEST_CASE ("Additive: FM sidebands and AM", "[additive]")
{
    const double f = 400.0;
    {
        AddRig r;
        r.shape (add::Sine);
        r.hz (f);
        const auto dry = r.render (9600);
        CHECK (mag (dry, 2 * f) < 1.0e-5);
    }
    {
        // FM Amount 0.5, ratio 1: deviation 200 Hz → index β = 0.5, J1(0.5)/J0(0.5) ≈ 0.2577
        AddRig r;
        r.shape (add::Sine);
        r.set (lp::addFmAmount, 0.5f);
        r.hz (f);
        const auto x = r.render (9600);
        CHECK (mag (x, 2 * f) / mag (x, f) == Catch::Approx (0.2577).epsilon (0.05));
        for (float v : x) REQUIRE (std::isfinite (v));
    }
    {
        AddRig r;
        r.shape (add::Sine);
        r.set (lp::addAmAmount, 1.0f);
        r.set (lp::addAmFreq, 100.0f);
        r.hz (1000.0);
        const auto x = r.render (9600);
        CHECK (mag (x, 1100.0) / mag (x, 1000.0) == Catch::Approx (0.5).epsilon (0.02));
        CHECK (mag (x, 900.0) / mag (x, 1000.0) == Catch::Approx (0.5).epsilon (0.02));
    }
    {
        // AM Depth 100%: even partials in counter-phase (at m = −1 only the even ones sound)
        AddRig r;
        r.customOnly ({ 1, 2 });
        r.set (lp::addAmAmount, 1.0f);
        r.set (lp::addAmFreq, 1.0f);
        r.set (lp::addAmDepth, 1.0f);
        r.hz (300.0);
        const auto x = r.render (1920, (int) (0.73 * fs));
        CHECK (mag (x, 600.0) > 0.4);
        CHECK (mag (x, 300.0) < 0.02);
    }
    for (int w = 0; w < 4; ++w)
    {
        AddRig r;
        r.shape (add::Sine);
        r.set (lp::addAmAmount, 0.5f);
        r.set (lp::addAmFreq, 300.0f);
        r.set (lp::addAmWave, (float) w);
        r.hz (1000.0);
        const auto x = r.render (4800);
        for (float v : x) REQUIRE (std::abs (v) <= 0.6f);
        CHECK (mag (x, 1300.0) > 0.02);
    }
}

TEST_CASE ("Additive: stereo width, spread, position, mid/side", "[additive]")
{
    const double f = 300.0;
    {
        AddRig r;
        r.customOnly ({ 1, 2 });
        r.set (lp::addSpread, 1.0f);
        r.hz (f);
        std::vector<float> right;
        const auto left = r.render (9600, 2400, &right);
        CHECK (mag (left, 2 * f) < 1.0e-4);          // partial 2 hard right
        CHECK (mag (right, 2 * f) > 0.6);
        CHECK (mag (left, f) == Catch::Approx (mag (right, f)).epsilon (1e-3));   // fundamental centred
    }
    {
        AddRig r;
        r.customOnly ({ 1, 2 });
        r.set (lp::addSpread, 1.0f);
        r.set (lp::addWidth, 0.0f);
        r.hz (f);
        std::vector<float> right;
        const auto left = r.render (4800, 2400, &right);
        for (size_t i = 0; i < left.size(); ++i) REQUIRE (left[i] == Catch::Approx (right[i]).margin (1e-6));
    }
    {
        AddRig r;
        r.customOnly ({ 1, 2 });
        r.set (lp::addStereoPos, 1.0f);   // low partials right
        r.set (lp::addPartials, 2.0f);
        r.hz (f);
        std::vector<float> right;
        const auto left = r.render (9600, 2400, &right);
        CHECK (mag (left, f) < 1.0e-4);
        CHECK (mag (right, 2 * f) < 1.0e-4);
    }
    {
        AddRig r;
        r.customOnly ({ 1, 2 });
        r.set (lp::addSpread, 1.0f);
        r.set (lp::addMsBalance, -1.0f);   // mid only
        r.hz (f);
        std::vector<float> right;
        const auto left = r.render (4800, 2400, &right);
        for (size_t i = 0; i < left.size(); ++i) REQUIRE (left[i] == Catch::Approx (right[i]).margin (1e-6));
    }
    {
        AddRig r;
        r.shape (add::Saw);
        r.set (lp::addStereoMod, 1.0f);
        r.hz (f);
        std::vector<float> right;
        const auto left = r.render (9600, 2400, &right);
        double diff = 0.0;
        for (size_t i = 0; i < left.size(); ++i) diff += std::abs (left[i] - right[i]);
        CHECK (diff > 1.0);
    }
}

TEST_CASE ("Additive: unison, legato, release, silence", "[additive]")
{
    {
        AddRig r;
        r.shape (add::Saw);
        r.p.unison = 4;
        r.p.detuneCents = 20.0f;
        r.p.spread = 1.0f;
        r.hz (220.0);
        std::vector<float> right;
        const auto x = r.render (9600, 2400, &right);
        CHECK (rms (x) > 0.05);
        for (float v : x) REQUIRE (std::isfinite (v));
    }
    {
        // pitch glide inside the block stays continuous (no clicks): max sample step bounded
        AddRig r;
        r.shape (add::Sine);
        r.hz (200.0);
        r.render (32, 0);
        r.p.pitchStart = 60.0f;
        r.p.pitchEnd = 72.0f;
        const auto x = r.render (32, 0);
        for (size_t i = 1; i < x.size(); ++i) REQUIRE (std::abs (x[i] - x[i - 1]) < 0.05f);
    }
    {
        AddRig r;
        r.shape (add::Saw);
        r.hz (300.0);
        r.render (4800, 0);
        r.engine.kill (0);
        const auto x = r.render (480, 0);
        for (float v : x) REQUIRE (v == 0.0f);
    }
    {
        AddRig r;
        r.shape (add::Saw);
        r.set (lp::addPenvAmount, 1.0f);
        r.set (lp::addPenvSustain, 1.0f);
        r.set (lp::addPenvRelease, 0.1f);
        r.set (lp::addPenvDist, 0.0f);
        r.hz (300.0);
        r.render (4800, 0);
        r.engine.noteOff (0);
        const auto x = r.render (2400, 9600);   // 200 ms after release
        CHECK (rms (x) < 1.0e-4);
    }
}

TEST_CASE ("Additive: through the synth (Custom spectrum, state, copy/paste)", "[additive]")
{
    test::Rig rig;
    test::setParam (rig.proc, "layer1_engine", 1.0f);   // Additive
    auto& store = rig.synth().layerStore (0);
    store.set (lp::addSpectrumA, (float) add::Custom);
    auto& c = store.additiveSpectrum (0);
    for (int k = 0; k < add::maxPartials; ++k) c.set (add::fAmp, k, k == 2 ? 0.9f : 0.0f);
    rig.noteOn (57);
    float peak = 0.0f;
    for (int b = 0; b < 40; ++b)
    {
        rig.run();
        for (int i = 0; i < rig.buffer.getNumSamples(); ++i) peak = std::max (peak, std::fabs (rig.buffer.getSample (0, i)));
    }
    CHECK (peak > 0.05f);

    const auto state = rig.synth().saveState();
    c.set (add::fAmp, 2, 0.1f);
    rig.synth().loadState (state);
    CHECK (store.additiveSpectrum (0).get (add::fAmp, 2) == Catch::Approx (0.9f));
    CHECK (store.additiveSpectrum (0).get (add::fAmp, 0) == 0.0f);

    const auto layer = rig.synth().copyLayer (0);
    rig.synth().pasteLayer (3, layer);
    CHECK (rig.synth().layerStore (3).additiveSpectrum (0).get (add::fAmp, 2) == Catch::Approx (0.9f));
    rig.synth().initLayer (3);
    CHECK (rig.synth().layerStore (3).additiveSpectrum (0).get (add::fAmp, 2) == Catch::Approx (1.0f / 3.0f));
}

TEST_CASE ("Additive: CPU of 24 voices x 64 partials", "[.bench]")
{
    AddRig r (24);
    r.shape (add::Saw);
    float bl[32], br[32];
    for (int v = 0; v < 24; ++v)
    {
        const float pitch = 36.0f + (float) v * 2.0f;
        r.engine.noteOn (v, { (int) pitch, 1.0f, 0, true });
    }
    auto run = [&] (int seconds)
    {
        const int blocks = (int) fs * seconds / 32;
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b)
            for (int v = 0; v < 24; ++v)
            {
                r.p.pitchStart = r.p.pitchEnd = 36.0f + (float) v * 2.0f;
                r.engine.render (v, r.p, bl, br, 32);
            }
        return 100.0 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / seconds;
    };
    run (1);
    WARN ("Additive 24 voices x 64 partials: " << run (5) << " % of one core @48 kHz");
    r.set (lp::addFmAmount, 0.3f);
    r.set (lp::addPenvAmount, 0.5f);
    r.set (lp::addStereoMod, 0.5f);
    WARN ("  + FM, partial ENV, stereo mod: " << run (3) << " % of one core");
    r.set (lp::addPartials, 256.0f);
    r.set (lp::addFmAmount, 0.0f);
    r.set (lp::addPenvAmount, 0.0f);
    r.set (lp::addStereoMod, 0.0f);
    WARN ("  256 partials: " << run (2) << " % of one core");
}
