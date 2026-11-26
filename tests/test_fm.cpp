#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "engines/fm/FmEngine.h"
#include "engines/fm/FmSysex.h"
#include "TestHelpers.h"
#include <chrono>
#include <cmath>
#include <vector>

using chroma::FmEngine;
namespace lp = chroma::lp;

namespace
{
constexpr double fs = 48000.0;
constexpr double pi = 3.14159265358979323846;

struct FmRig
{
    FmEngine engine;
    std::array<float, lp::count> base {};
    std::array<float, 4> lfo {};
    chroma::EngineVoiceParams p;

    explicit FmRig (int oversampling = 2)
    {
        for (int i = 0; i < lp::count; ++i)
            base[(size_t) i] = lp::descs[(size_t) i].def;
        engine.prepare (fs, 32, 1);
        engine.setOversampling (oversampling);
        p.base = base.data();
        p.lfo = lfo.data();
        set (lp::fmOutput, 1.0f);
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

// Hann-windowed DFT magnitude at one frequency (amplitude-normalised for a sine).
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

double besselJ (int n, double x)
{
    double sum = 0.0;
    for (int k = 0; k < 30; ++k)
        sum += std::pow (-1.0, k) / (std::tgamma (k + 1.0) * std::tgamma (k + n + 1.0)) * std::pow (x / 2.0, 2 * k + n);
    return sum;
}

double db (double a) { return 20.0 * std::log10 (std::max (a, 1.0e-12)); }

void setAlgorithm (FmRig& r, int alg1Based)
{
    r.set (lp::fmAlgorithm, (float) (alg1Based - 1));
    r.set (lp::fmAlgorithmB, (float) (alg1Based - 1));
}
} // namespace

TEST_CASE ("FM6: classic algorithm table", "[fm]")
{
    for (int a = 0; a < FmEngine::numAlgorithms; ++a)
    {
        const auto& alg = FmEngine::classic (a);
        float carriers = 0.0f;
        for (int i = 0; i < 6; ++i)
        {
            carriers += alg.carrier[(size_t) i];
            for (int s = 0; s <= i; ++s)   // modulators always have a higher number than their target
                REQUIRE (alg.conn[(size_t) i][(size_t) s] == 0.0f);
        }
        REQUIRE (carriers >= 1.0f);
    }
    CHECK (FmEngine::classic (0).carrier[0] == 1.0f);
    CHECK (FmEngine::classic (0).conn[4][5] == 1.0f);                 // 6 → 5
    CHECK (FmEngine::classic (31).carrier == std::array<float, 6> { 1, 1, 1, 1, 1, 1 });
    CHECK (FmEngine::classic (3).fb[5][3] == 1.0f);                   // alg 4: op 6 feedback loop from op 4
    CHECK (FmEngine::classic (3).fb[5][5] == 0.0f);
    CHECK (FmEngine::classic (5).fb[5][4] == 1.0f);                   // alg 6: from op 5
    CHECK (FmEngine::classic (1).feedbackOp == 1);                    // alg 2: feedback on op 2
    CHECK (FmEngine::classic (15).carrier == std::array<float, 6> { 1, 0, 0, 0, 0, 0 });
}

TEST_CASE ("FM6: single operator is a clean sine at the note pitch", "[fm]")
{
    FmRig r;
    r.hz (440.0);
    const auto x = r.render (16384);
    CHECK (mag (x, 440.0) == Catch::Approx (1.0).margin (0.02));
    CHECK (db (mag (x, 880.0)) < -80.0);
    CHECK (db (mag (x, 1320.0)) < -80.0);
    CHECK (std::fabs (peakFreq (x, 400.0, 480.0, 0.5) - 440.0) < 1.0);
}

TEST_CASE ("FM6: two-operator FM sidebands follow Bessel functions", "[fm]")
{
    FmRig r;
    setAlgorithm (r, 5);                         // 2 → 1
    r.set (lp::fmOpRatio[0], 4.0f);              // carrier 1000 Hz
    r.set (lp::fmOpRatio[1], 1.0f);              // modulator 250 Hz
    const float level = 0.75f;
    r.set (lp::fmOpLevel[1], level);
    r.hz (250.0);
    const auto x = r.render (32768);
    const double beta = 2.0 * pi * FmEngine::modScale * FmEngine::levelToAmp (level);
    const double c = mag (x, 1000.0);
    for (int k = 1; k <= 2; ++k)
    {
        const double expected = std::fabs (besselJ (k, beta) / besselJ (0, beta));
        CHECK (std::fabs (db (mag (x, 1000.0 + 250.0 * k) / c) - db (expected)) < 0.5);
        CHECK (std::fabs (db (mag (x, 1000.0 - 250.0 * k) / c) - db (expected)) < 0.5);
    }
    CHECK (std::fabs (c - std::fabs (besselJ (0, beta))) < 0.02);
}

TEST_CASE ("FM6: Mod Index scales modulation, 0 = pure carrier", "[fm]")
{
    FmRig r;
    setAlgorithm (r, 5);
    r.set (lp::fmOpLevel[1], 0.9f);
    r.set (lp::fmModIndex, 0.0f);
    r.hz (300.0);
    auto x = r.render (16384);
    CHECK (db (mag (x, 600.0)) < -80.0);
    r.set (lp::fmModIndex, 1.0f);
    x = r.render (16384, 960);
    CHECK (db (mag (x, 600.0)) > -20.0);
}

TEST_CASE ("FM6: feedback adds harmonics and stays bounded", "[fm]")
{
    double prevH2 = -1.0;
    for (float fb : { 0.0f, 0.3f, 0.6f, 1.0f })
    {
        FmRig r;
        r.set (lp::fmOpFeedback[0], fb);
        r.hz (220.0);
        const auto x = r.render (16384);
        double peak = 0.0;
        for (float v : x) { REQUIRE (std::isfinite (v)); peak = std::max (peak, (double) std::fabs (v)); }
        CHECK (peak < 1.5);
        double h2 = 0.0;   // harmonic content above the fundamental, relative to it
        for (int k = 2; k <= 8; ++k) h2 += mag (x, 220.0 * k);
        h2 /= mag (x, 220.0);
        CHECK (db (mag (x, 330.0)) < -80.0);   // periodic, no chaotic noise
        CHECK (h2 > prevH2);
        prevH2 = h2;
    }
    CHECK (prevH2 > 1.0);
}

TEST_CASE ("FM6: frequency modes (Ratio, Coarse/Fine, Fixed)", "[fm]")
{
    {
        FmRig r;
        r.set (lp::fmOpRatio[0], 2.0f);
        r.set (lp::fmOpCoarse[0], 7.0f);
        r.set (lp::fmOpFine[0], 50.0f);
        r.hz (200.0);
        const double expect = 400.0 * std::exp2 (7.5 / 12.0);
        CHECK (std::fabs (peakFreq (r.render (16384), expect - 20, expect + 20, 0.25) - expect) < 1.0);
    }
    {
        FmRig r;
        r.set (lp::fmOpMode[0], 1.0f);
        r.set (lp::fmOpFixed[0], 1000.0f);
        r.note (40.0f);
        auto x = r.render (16384);
        CHECK (mag (x, 1000.0) > 0.9);
        r.note (80.0f);
        x = r.render (16384, 480);
        CHECK (mag (x, 1000.0) > 0.9);
    }
}

TEST_CASE ("FM6: key scaling curves", "[fm]")
{
    // -Lin: full depth over 4 octaves; -Exp is gentler near the breakpoint; + curves raise the level.
    CHECK (FmEngine::keyScale (1.0f, 60, 60, 1.0f, 1.0f, 0, 0) == 1.0f);
    CHECK (FmEngine::keyScale (1.0f, 84, 60, 0.0f, 1.0f, 0, 0) == Catch::Approx (0.5f));
    CHECK (FmEngine::keyScale (1.0f, 36, 60, 1.0f, 0.0f, 0, 0) == Catch::Approx (0.5f));
    CHECK (FmEngine::keyScale (1.0f, 84, 60, 1.0f, 0.0f, 0, 0) == 1.0f);   // left depth only
    CHECK (FmEngine::keyScale (1.0f, 72, 60, 0.0f, 1.0f, 0, 1) > FmEngine::keyScale (1.0f, 72, 60, 0.0f, 1.0f, 0, 0));
    CHECK (FmEngine::keyScale (0.5f, 84, 60, 0.0f, 1.0f, 0, 3) == Catch::Approx (1.0f));
    CHECK (FmEngine::keyScale (0.5f, 108, 60, 0.0f, 1.0f, 0, 2) == Catch::Approx (1.0f));

    FmRig r;
    r.set (lp::fmOpKsRightDepth[0], 1.0f);
    r.note (84.0f);
    const double a = rms (r.render (8192));
    CHECK (db (a / std::sqrt (0.5)) == Catch::Approx (-36.0).margin (0.5));
}

TEST_CASE ("FM6: velocity sensitivity and level law", "[fm]")
{
    CHECK (db (FmEngine::levelToAmp (0.5f)) == Catch::Approx (-36.12).margin (0.05));
    CHECK (FmEngine::levelToAmp (0.0f) == 0.0f);
    FmRig hi, lo;
    for (auto* r : { &hi, &lo }) r->set (lp::fmOpVelSens[0], 1.0f);
    hi.hz (440.0, 1.0f);
    lo.hz (440.0, 0.5f);
    const double d = db (rms (lo.render (8192)) / rms (hi.render (8192)));
    CHECK (d == Catch::Approx (-0.5 * 0.583 * 72.25).margin (0.5));
}

TEST_CASE ("FM6: operator envelope, release and Amp silence", "[fm]")
{
    FmRig r;
    r.set (lp::fmOpSustain[0], 0.0f);
    r.set (lp::fmOpDecay[0], 0.05f);
    r.hz (440.0);
    CHECK (rms (r.render (480, 0)) > 0.1);
    CHECK (rms (r.render (4800, 9600)) < 1.0e-3);

    FmRig s;
    s.set (lp::fmOpRelease[0], 0.05f);
    s.hz (440.0);
    CHECK (rms (s.render (4800)) > 0.5);
    s.engine.noteOff (0);
    CHECK (rms (s.render (4800, 9600)) < 1.0e-3);
}

TEST_CASE ("FM6: disabled or silent operators produce nothing", "[fm]")
{
    FmRig r;
    r.set (lp::fmOpOn[0], 0.0f);
    r.hz (440.0);
    for (float v : r.render (4800)) REQUIRE (v == 0.0f);
}

TEST_CASE ("FM6: Custom matrix reproduces a classic algorithm; morph ends match A and B", "[fm]")
{
    auto setup = [] (FmRig& r)
    {
        for (size_t i = 0; i < 6; ++i)
        {
            r.set (lp::fmOpLevel[i], 0.6f + 0.05f * (float) i);
            r.set (lp::fmOpRatio[i], 1.0f + (float) i);
        }
        r.set (lp::fmOpFeedback[5], 0.5f);
        r.hz (220.0);
    };
    FmRig classic, custom;
    setup (classic); setup (custom);
    setAlgorithm (classic, 5);    // 2→1, 4→3, 6→5; carriers 1, 3, 5
    setAlgorithm (custom, 33);
    for (auto [s, d] : { std::pair { 1, 0 }, { 3, 2 }, { 5, 4 } })
    {
        const lp::Id ids[6][6] {};
        (void) ids;
        const std::array<const std::array<lp::Id, 6>*, 6> from { &lp::fmCustomFrom1, &lp::fmCustomFrom2, &lp::fmCustomFrom3,
                                                                  &lp::fmCustomFrom4, &lp::fmCustomFrom5, &lp::fmCustomFrom6 };
        custom.set ((*from[(size_t) s])[(size_t) d], 1.0f);
    }
    for (size_t i = 0; i < 6; ++i)
        custom.set (lp::fmCustomOut[i], (i == 0 || i == 2 || i == 4) ? 1.0f : 0.0f);
    const auto a = classic.render (4800), b = custom.render (4800);
    double err = 0.0;
    for (size_t i = 0; i < a.size(); ++i) err = std::max (err, (double) std::fabs (a[i] - b[i]));
    CHECK (err < 1.0e-5);

    // Morph = 100% renders algorithm B exactly; 50% sits in between.
    FmRig m, ref;
    setup (m); setup (ref);
    m.set (lp::fmAlgorithm, 31.0f);
    m.set (lp::fmAlgorithmB, 0.0f);
    m.set (lp::fmMorph, 1.0f);
    setAlgorithm (ref, 1);
    const auto x = m.render (4800), y = ref.render (4800);
    err = 0.0;
    for (size_t i = 0; i < x.size(); ++i) err = std::max (err, (double) std::fabs (x[i] - y[i]));
    CHECK (err < 1.0e-5);

    FmRig half;
    setup (half);
    half.set (lp::fmAlgorithm, 31.0f);
    half.set (lp::fmAlgorithmB, 0.0f);
    half.set (lp::fmMorph, 0.5f);
    for (float v : half.render (4800)) REQUIRE (std::isfinite (v));
}

TEST_CASE ("FM6: pitch envelope and LFO pitch depth", "[fm]")
{
    FmRig r;
    r.set (lp::fmPenvAmount, 12.0f);
    r.set (lp::fmPenvAttack, 0.001f);
    r.set (lp::fmPenvSustain, 1.0f);
    r.hz (300.0);
    CHECK (std::fabs (peakFreq (r.render (16384), 500.0, 700.0, 0.5) - 600.0) < 1.0);

    FmRig l;
    l.set (lp::fmLfoPitch[1], 12.0f);
    l.lfo[1] = -1.0f;
    l.hz (600.0);
    CHECK (std::fabs (peakFreq (l.render (16384), 250.0, 350.0, 0.5) - 300.0) < 1.0);
}

TEST_CASE ("FM6: LFO amp depth with AM sens; LFO op depth", "[fm]")
{
    FmRig r;
    r.set (lp::fmLfoAmp[0], 1.0f);
    r.set (lp::fmOpAmSens[0], 1.0f);
    r.lfo[0] = 0.0f;   // half depth
    r.hz (440.0);
    CHECK (mag (r.render (8192), 440.0) == Catch::Approx (0.5).margin (0.02));

    FmRig o;
    setAlgorithm (o, 5);
    o.set (lp::fmOpLevel[1], 0.8f);
    o.set (lp::fmLfoOp[0], 1.0f);
    o.lfo[0] = -1.0f;   // mod index × 0
    o.hz (300.0);
    CHECK (db (mag (o.render (8192), 600.0)) < -80.0);
}

TEST_CASE ("FM6: operator Sync locks to the fundamental", "[fm]")
{
    FmRig r;
    r.set (lp::fmOpRatio[0], 2.37f);
    r.set (lp::fmOpSync[0], 1.0f);
    r.hz (200.0);
    const auto x = r.render (32768);
    double harm = 0.0, off = 0.0;
    for (int k = 1; k <= 6; ++k) harm += mag (x, 200.0 * k);
    off = mag (x, 474.0);
    CHECK (harm > 0.3);
    CHECK (db (off) < db (harm) - 20.0);
}

TEST_CASE ("FM6: waves are bounded and distinct", "[fm]")
{
    for (int w = 0; w < 8; ++w)
    {
        double mx = 0.0, diff = 0.0;
        for (int i = 0; i < 1000; ++i)
        {
            const double ph = i / 1000.0;
            const float v = FmEngine::wave (w, ph);
            mx = std::max (mx, (double) std::fabs (v));
            diff += std::fabs (v - FmEngine::wave (0, ph));
        }
        CHECK (mx <= 1.05);
        CHECK (mx > 0.9);
        if (w > 0) CHECK (diff > 10.0);
    }
}

TEST_CASE ("FM6: unison and operator pan spread the image", "[fm]")
{
    FmRig r;
    r.set (lp::fmOpPan[0], 1.0f);
    r.hz (440.0);
    std::vector<float> right;
    const auto left = r.render (4800, 4800, &right);
    CHECK (rms (left) < 1.0e-4);
    CHECK (rms (right) > 0.5);

    FmRig u;
    u.p.unison = 4;
    u.p.detuneCents = 30.0f;
    u.p.spread = 1.0f;
    u.hz (440.0);
    const auto l2 = u.render (4800, 4800, &right);
    double diff = 0.0;
    for (size_t i = 0; i < l2.size(); ++i) diff += std::fabs (l2[i] - right[i]);
    CHECK (diff > 1.0);
}

TEST_CASE ("FM6: high index stays alias-limited with oversampling", "[fm]")
{
    // Bright 2-op patch at a high note: energy outside the harmonic series stays well below the signal.
    auto inharm = [] (int os)
    {
        FmRig r (os);
        setAlgorithm (r, 5);
        r.set (lp::fmOpLevel[1], 0.95f);
        r.hz (1760.0);
        const auto x = r.render (16384);
        double h = 0.0, a = 0.0;
        for (int k = 1; k * 1760.0 < 20000.0; ++k) h += mag (x, 1760.0 * k);
        for (double f = 900.0; f < 20000.0; f += 1760.0) a = std::max (a, mag (x, f));
        return db (a) - db (h);
    };
    const double a1 = inharm (1), a4 = inharm (4);
    CHECK (a4 < a1);
    CHECK (a4 < -60.0);
}

TEST_CASE ("FM6: .syx 32-voice bank import", "[fm]")
{
    std::vector<uint8_t> bank (4104, 0);
    bank[0] = 0xF0; bank[1] = 0x43; bank[2] = 0x00; bank[3] = 0x09; bank[4] = 0x20; bank[5] = 0x00;
    for (int v = 0; v < 32; ++v)
    {
        uint8_t* d = bank.data() + 6 + v * 128;
        for (int k = 0; k < 6; ++k)
        {
            uint8_t* o = d + k * 17;   // op 6 first
            o[0] = 99; o[1] = 50; o[2] = 50; o[3] = 60;
            o[4] = 99; o[5] = 90; o[6] = 80; o[7] = 0;
            o[8] = 39;                 // breakpoint C3
            o[11] = 0 | (3 << 2);      // -Lin / +Lin
            o[12] = 7 | (10 << 3);     // rate scaling 7, detune +3
            o[13] = 3 | (7 << 2);
            o[14] = (uint8_t) (99 - k);
            o[15] = (uint8_t) (2 << 1); o[16] = 50;   // ratio 2 × 1.5
        }
        d[5 * 17 + 15] = 1 | (2 << 1); d[5 * 17 + 16] = 0;   // op 1: fixed 100 Hz
        for (int i = 102; i < 110; ++i) d[i] = 50;
        d[110] = (uint8_t) v;          // algorithm v + 1
        d[111] = 7 | (1 << 3);
        d[112] = 35; d[114] = 20; d[115] = 10; d[116] = 1 | (4 << 1) | (3 << 4);
        d[117] = 24;
        const char* name = "BANK TEST ";
        for (int c = 0; c < 10; ++c) d[118 + c] = (uint8_t) name[c];
    }
    int sum = 0;
    for (int i = 0; i < 4096; ++i) sum += bank[(size_t) 6 + (size_t) i];
    bank[4102] = (uint8_t) ((-sum) & 0x7f);
    bank[4103] = 0xF7;

    std::vector<chroma::FmPatch> patches;
    REQUIRE (chroma::parseFmBank (bank.data(), bank.size(), patches));
    REQUIRE (patches.size() == 32);
    CHECK (patches[0].name == "BANK TEST");

    auto value = [] (const chroma::FmPatch& p, int id)
    {
        for (auto& [k, v] : p.values) if (k == id) return v;
        return -999.0f;
    };
    const auto& p = patches[3];
    CHECK (value (p, lp::fmAlgorithm) == 3.0f);
    CHECK (value (p, lp::fmOpFeedback[5]) == Catch::Approx (1.0f));   // alg 4 feedback on op 6
    CHECK (value (p, lp::fmOpFeedback[0]) == 0.0f);
    CHECK (value (patches[1], lp::fmOpFeedback[1]) == Catch::Approx (1.0f));   // alg 2 → op 2
    CHECK (value (p, lp::fmOpRatio[1]) == Catch::Approx (3.0f));
    CHECK (value (p, lp::fmOpMode[0]) == 1.0f);
    CHECK (value (p, lp::fmOpFixed[0]) == Catch::Approx (100.0f));
    CHECK (value (p, lp::fmOpLevel[5]) == Catch::Approx (1.0f));    // op 6 stored first, level 99
    CHECK (value (p, lp::fmOpLevel[0]) == Catch::Approx (94.0f / 99.0f));
    CHECK (value (p, lp::fmOpKsBreak[0]) == 60.0f);
    CHECK (value (p, lp::fmOpKsRightCurve[0]) == 3.0f);
    CHECK (value (p, lp::fmOpDetune[0]) == 3.0f);
    CHECK (value (p, lp::fmOpRateScale[0]) == Catch::Approx (1.0f));
    CHECK (value (p, lp::fmOpReset[0]) == 1.0f);
    CHECK (value (p, lp::fmPenvAmount) == 0.0f);
    CHECK (value (p, lp::lfoShape[0]) == 0.0f);                       // sine
    CHECK (value (p, lp::lfoMode[0]) == 1.0f);
    CHECK (value (p, lp::pitch) == 0.0f);
    CHECK (value (p, lp::fmOpSustain[0]) < 1.0f);

    // Every value is within its parameter range.
    for (auto& [id, v] : p.values)
    {
        const auto& d = lp::descs[(size_t) id];
        CHECK ((v >= d.min && v <= d.max));
    }

    bank[4102] ^= 1;
    CHECK_FALSE (chroma::parseFmBank (bank.data(), bank.size(), patches));
    CHECK (patches.empty());
    CHECK_FALSE (chroma::parseFmBank (bank.data(), 100, patches));
    CHECK (chroma::parseFmBank (bank.data() + 6, 4096, patches));     // raw voice data
}

TEST_CASE ("FM6: plays through the synth when selected as layer engine", "[fm]")
{
    test::Rig rig;
    test::setParam (rig.proc, chroma::ParamIDs::layerEngine[0], 2.0f);
    auto& store = rig.synth().layerStore (0);
    store.set (lp::fmAlgorithm, 0.0f);
    store.set (lp::fmOpLevel[1], 0.8f);
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

    // .syx patch applied to the layer store still plays
    std::vector<uint8_t> raw (4096, 0);
    for (int v = 0; v < 32; ++v)
    {
        uint8_t* d = raw.data() + v * 128;
        for (int k = 0; k < 6; ++k) { d[k * 17 + 0] = 99; d[k * 17 + 4] = 99; d[k * 17 + 6] = 99; d[k * 17 + 14] = 99; d[k * 17 + 15] = 2; }
        d[110] = 31;
        for (int i = 102; i < 110; ++i) d[i] = 50;
        d[117] = 24;
    }
    std::vector<chroma::FmPatch> patches;
    REQUIRE (chroma::parseFmBank (raw.data(), raw.size(), patches));
    for (auto& [id, v] : patches[0].values) store.set (id, v);
    rig.noteOff (60);
    rig.run ({}, 100);
    rig.noteOn (64);
    e = 0.0;
    for (int b = 0; b < 20; ++b)
    {
        rig.run();
        for (int i = 0; i < rig.buffer.getNumSamples(); ++i) e += (double) rig.buffer.getSample (0, i) * rig.buffer.getSample (0, i);
    }
    CHECK (e > 1.0);
}

TEST_CASE ("FM6: CPU of one voice (6 ops, feedback, 2x)", "[.bench]")
{
    FmRig r (2);
    setAlgorithm (r, 1);
    for (size_t i = 0; i < 6; ++i) r.set (lp::fmOpLevel[i], 0.8f);
    r.set (lp::fmOpFeedback[5], 0.5f);
    r.hz (440.0);
    const int seconds = 5;
    const auto t0 = std::chrono::steady_clock::now();
    r.render ((int) fs * seconds, 0);
    const double el = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    WARN ("FM6 voice: " << 100.0 * el / seconds << " % of one core @48 kHz");
}
