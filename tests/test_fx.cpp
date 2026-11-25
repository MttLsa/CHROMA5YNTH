#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "shared/fx/FxChain.h"
#include "TestHelpers.h"
#include <chrono>
#include <cmath>
#include <vector>

using namespace chroma::fx;
namespace lp = chroma::lp;

namespace
{
constexpr double fs = 48000.0;
constexpr float tpi = 6.283185307f;

struct Stereo
{
    std::vector<float> l, r;
    explicit Stereo (size_t n) : l (n, 0.0f), r (n, 0.0f) {}
};

Stereo sine (float hz, float amp, size_t n)
{
    Stereo s (n);
    for (size_t i = 0; i < n; ++i) s.l[i] = s.r[i] = amp * std::sin (tpi * hz * (float) i / (float) fs);
    return s;
}

template <typename Fx>
void run (Fx& fx, Stereo& s, int block = 32)
{
    for (size_t i = 0; i < s.l.size(); i += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, s.l.size() - i);
        fx.process (s.l.data() + i, s.r.data() + i, n);
    }
}

float rms (const std::vector<float>& x, size_t from, size_t to)
{
    double a = 0.0;
    for (size_t i = from; i < to; ++i) a += (double) x[i] * x[i];
    return (float) std::sqrt (a / (double) (to - from));
}
float db (float g) { return 20.0f * std::log10 (std::max (g, 1.0e-12f)); }

bool finite (const Stereo& s, float limit = 100.0f)
{
    for (size_t i = 0; i < s.l.size(); ++i)
        if (! std::isfinite (s.l[i]) || ! std::isfinite (s.r[i]) || std::fabs (s.l[i]) > limit || std::fabs (s.r[i]) > limit)
            return false;
    return true;
}

// Steady-state gain (dB) of a linear stereo FX at one frequency (left channel).
template <typename Fx>
float gainAt (Fx& fx, float hz)
{
    fx.reset();
    auto s = sine (hz, 0.25f, 48000);
    run (fx, s);
    return db (rms (s.l, 24000, 48000) / (0.25f / std::sqrt (2.0f)));
}
} // namespace

TEST_CASE ("FX: distortion types, loudness and dry/wet alignment", "[fx]")
{
    for (int t = 0; t <= (int) DistType::Tape; ++t)
    {
        Distortion d;
        d.prepare (fs);
        d.setParams ({ (DistType) t, 0.8f, 0.5f, 1.0f, 0.0f });
        auto s = sine (220.0f, 0.5f, 24000);
        run (d, s);
        INFO ("type " << t);
        REQUIRE (finite (s, 4.0f));
        const float out = rms (s.l, 4800, 24000);
        CHECK (out > 0.05f);
        CHECK (db (out / 0.3536f) < 9.0f);   // loudness compensated
    }

    // Low drive + 50% mix: the 2x path latency is compensated on the dry signal (no comb filtering).
    Distortion d;
    d.prepare (fs);
    REQUIRE (d.getOversamplingLatency() > 0);
    d.setParams ({ DistType::Soft, 0.0f, 0.5f, 0.5f, 0.0f });
    for (float hz : { 1000.0f, 6000.0f, 11000.0f })
    {
        d.reset();
        d.setParams ({ DistType::Soft, 0.0f, 0.5f, 0.5f, 0.0f });
        auto s = sine (hz, 0.02f, 24000);
        run (d, s);
        CHECK (std::fabs (db (rms (s.l, 4800, 24000) / (0.02f / std::sqrt (2.0f)))) < 1.0f);
    }

    // Harmonic content grows with drive.
    auto thd = [] (float drive)
    {
        Distortion x; x.prepare (fs); x.setParams ({ DistType::Tube, drive, 1.0f, 1.0f, 0.0f });
        auto s = sine (1000.0f, 0.1f, 48000);
        run (x, s);
        double si = 0.0, co = 0.0, tot = 0.0;
        for (size_t i = 24000; i < 48000; ++i)
        {
            const float ph = tpi * 1000.0f * (float) i / (float) fs;
            si += (double) s.l[i] * std::sin (ph);
            co += (double) s.l[i] * std::cos (ph);
            tot += (double) s.l[i] * s.l[i];
        }
        const double f = 2.0 * (si * si + co * co) / 24000.0;
        return (tot - f) / tot;
    };
    CHECK (thd (0.0f) < 0.01);
    CHECK (thd (0.8f) > 0.05);

    // Bitcrush quantises.
    Distortion b; b.prepare (fs); b.setParams ({ DistType::Bitcrush, 1.0f, 1.0f, 1.0f, 0.0f });
    auto s = sine (100.0f, 0.9f, 4800);
    run (b, s);
    REQUIRE (finite (s));
}

TEST_CASE ("FX: chorus modes and flanger", "[fx]")
{
    for (int m = 0; m <= (int) ChorusMode::Custom; ++m)
    {
        Chorus c;
        c.prepare (fs);
        ChorusParams p; p.mode = (ChorusMode) m; p.mix = 1.0f; p.feedback = 0.5f;
        c.setParams (p);
        auto s = sine (440.0f, 0.5f, 48000);
        const auto dry = s;
        run (c, s);
        INFO ("mode " << m);
        REQUIRE (finite (s, 2.0f));
        double diff = 0.0, lr = 0.0;
        for (size_t i = 4800; i < 48000; ++i) { diff += std::fabs (s.l[i] - dry.l[i]); lr += std::fabs (s.l[i] - s.r[i]); }
        CHECK (diff > 100.0);
        CHECK (lr > 10.0);    // stereo
    }

    Flanger f;
    f.prepare (fs);
    f.setParams ({ 0.5f, 1.0f, 0.95f, 0.5f, 1.0f, 0.5f });
    Stereo s (96000);
    chroma::fx::Rng rng;
    for (size_t i = 0; i < s.l.size(); ++i) s.l[i] = s.r[i] = 0.5f * rng.next();
    run (f, s);
    REQUIRE (finite (s, 4.0f));
}

TEST_CASE ("FX: phaser notch position", "[fx]")
{
    Phaser p;
    p.prepare (fs);
    // Depth 0: static 4-stage all-pass at 1 kHz -> -180 deg (notch with 50% mix) at fc * tan(22.5 deg).
    p.setParams ({ 0.1f, 0.0f, 0.0f, 1000.0f, 0.5f, 4 });
    const float notch = 1000.0f * std::tan (3.14159265f / 8.0f);
    CHECK (gainAt (p, notch) < -30.0f);
    CHECK (gainAt (p, 30.0f) > -1.0f);
    CHECK (gainAt (p, 15000.0f) > -1.0f);

    for (int st = 2; st <= 12; ++st)
    {
        p.reset();
        p.setParams ({ 2.0f, 1.0f, 0.95f, 2000.0f, 0.5f, st });
        auto s = sine (500.0f, 0.5f, 24000);
        run (p, s);
        REQUIRE (finite (s, 4.0f));
    }
}

TEST_CASE ("FX: delay times, feedback, ping-pong, multitap, sync", "[fx]")
{
    auto impulse = [] (Delay& d, DelayParams p, bool pingPong, size_t n)
    {
        d.reset();
        d.pingPong = pingPong;
        d.setParams (p);
        Stereo s (n);
        s.l[0] = s.r[0] = 1.0f;
        run (d, s);
        return s;
    };
    // Windowed sum around an echo (the feedback LP/HP smear the impulse but keep its area).
    auto peakNear = [] (const std::vector<float>& x, size_t at) { float m = 0.0f; for (size_t i = at - 12; i <= at + 12; ++i) m += x[i]; return std::fabs (m); };

    Delay d;
    d.prepare (fs);
    DelayParams p; p.timeL = 0.1f; p.timeR = 0.15f; p.feedback = 0.5f; p.mix = 1.0f; p.lpHz = 20000.0f;
    auto s = impulse (d, p, false, 24000);
    CHECK (peakNear (s.l, 4800) > 0.8f);
    CHECK (peakNear (s.l, 9600) == Catch::Approx (0.5f * peakNear (s.l, 4800)).margin (0.06));
    CHECK (peakNear (s.r, 7200) > 0.8f);
    CHECK (peakNear (s.r, 4800) < 0.01f);

    // Ping-pong: first echo left, second right.
    p.timeR = p.timeL;
    s = impulse (d, p, true, 24000);
    CHECK (peakNear (s.l, 4800) > 0.4f);
    CHECK (peakNear (s.r, 4800) < 0.01f);
    CHECK (peakNear (s.r, 9600) > 0.2f);

    // Multitap: taps at 1/4, 1/2, 3/4, 1 of the time.
    p.type = DelayType::Multitap; p.timeL = 0.2f; p.feedback = 0.0f;
    s = impulse (d, p, false, 24000);
    CHECK (peakNear (s.l, 2400) > 0.2f);
    CHECK (peakNear (s.r, 4800) > 0.2f);
    CHECK (peakNear (s.l, 7200) > 0.2f);
    CHECK (peakNear (s.l, 9600) > 0.2f);

    // Every type stays bounded at maximum feedback.
    for (int t = 0; t <= (int) DelayType::Multitap; ++t)
    {
        DelayParams q; q.type = (DelayType) t; q.timeL = 0.05f; q.timeR = 0.07f; q.feedback = 0.98f; q.mix = 1.0f; q.modulation = 1.0f;
        d.reset(); d.setParams (q);
        auto x = sine (300.0f, 0.9f, 96000);
        run (d, x);
        INFO ("type " << t);
        REQUIRE (finite (x, 60.0f));
    }

    CHECK (divisionSeconds (2, 120.0) == Catch::Approx (0.5f));       // 1/4
    CHECK (divisionSeconds (15, 120.0) == Catch::Approx (0.375f));    // 1/8D
    CHECK (divisionSeconds (8, 120.0) == Catch::Approx (1.0f / 3.0f)); // 1/4T
}

TEST_CASE ("FX: reverb decay time and stability", "[fx]")
{
    // RT60 from the Schroeder backward integral (T20 fit, -5..-25 dB).
    auto rt60 = [] (ReverbType type, float decay)
    {
        Reverb r;
        r.prepare (fs);
        ReverbParams p; p.type = type; p.decay = decay; p.mix = 1.0f; p.damping = 0.0f; p.predelayMs = 0.0f;
        p.highCut = 20000.0f; p.modulation = 0.0f;
        r.setParams (p);
        const size_t n = (size_t) (fs * decay * 1.5);
        Stereo s (n);
        s.l[0] = s.r[0] = 1.0f;
        run (r, s);
        std::vector<double> e (n + 1, 0.0);
        for (size_t i = n; i-- > 0;) e[i] = e[i + 1] + (double) s.l[i] * s.l[i] + (double) s.r[i] * s.r[i];
        size_t a = 0, b = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const double lvl = 10.0 * std::log10 (e[i] / e[0]);
            if (a == 0 && lvl < -5.0) a = i;
            if (b == 0 && lvl < -25.0) { b = i; break; }
        }
        return b > a ? 3.0f * (float) (b - a) / (float) fs : 0.0f;
    };
    for (auto t : { ReverbType::Room, ReverbType::Plate, ReverbType::Hall, ReverbType::LargeHall })
    {
        INFO ("type " << (int) t);
        CHECK (rt60 (t, 1.5f) == Catch::Approx (1.5f).epsilon (0.35));
        CHECK (rt60 (t, 4.0f) == Catch::Approx (4.0f).epsilon (0.35));
    }

    // Long decay, full modulation, loud input: bounded, no NaN.
    for (auto t : { ReverbType::Room, ReverbType::Plate, ReverbType::Hall, ReverbType::LargeHall })
    {
        Reverb r;
        r.prepare (fs);
        ReverbParams p; p.type = t; p.decay = 20.0f; p.size = 1.0f; p.modulation = 1.0f; p.diffusion = 1.0f; p.mix = 1.0f;
        r.setParams (p);
        auto s = sine (200.0f, 1.0f, 240000);
        run (r, s);
        REQUIRE (finite (s, 50.0f));
    }
}

TEST_CASE ("FX: EQ, compressor, master filter", "[fx]")
{
    Eq3 eq;
    eq.prepare (fs);
    EqParams e; e.lowGain = 12.0f; e.midGain = -6.0f; e.highGain = 9.0f;
    eq.setParams (e);
    CHECK (gainAt (eq, 25.0f) == Catch::Approx (12.0f).margin (1.0));
    CHECK (gainAt (eq, 1000.0f) == Catch::Approx (-6.0f).margin (0.6));
    CHECK (gainAt (eq, 18000.0f) == Catch::Approx (9.0f).margin (1.0));

    CHECK (Compressor::gainReduction (-10.0f, -20.0f, 4.0f, 0.0f) == Catch::Approx (7.5f));
    CHECK (Compressor::gainReduction (-30.0f, -20.0f, 4.0f, 6.0f) == Catch::Approx (0.0f));
    Compressor c;
    c.prepare (fs);
    c.setParams ({ -20.0f, 4.0f, 1.0f, 50.0f, 0.0f, 0.0f, 1.0f });
    auto s = sine (1000.0f, 1.0f, 48000);
    run (c, s);
    float peak = 0.0f;
    for (size_t i = 24000; i < 48000; ++i) peak = std::max (peak, std::fabs (s.l[i]));
    CHECK (db (peak) == Catch::Approx (-15.0f).margin (1.0));

    MasterFilter f;
    f.prepare (fs);
    MasterFilterParams m;   // defaults: HP 20 Hz, LP 20 kHz = open
    f.setParams (m);
    CHECK (std::fabs (gainAt (f, 1000.0f)) < 0.3f);
    CHECK (gainAt (f, 15000.0f) > -1.5f);
    m.hpHz = 2000.0f; f.reset(); f.setParams (m);
    CHECK (gainAt (f, 200.0f) < -35.0f);
    m.hpHz = 20.0f; m.lpHz = 500.0f; f.reset(); f.setParams (m);
    CHECK (gainAt (f, 5000.0f) < -35.0f);
    m.lpHz = 20000.0f; m.bpOn = true; m.bpHz = 1000.0f; m.bpWidth = 0.0f; f.reset(); f.setParams (m);
    CHECK (gainAt (f, 1000.0f) > -3.0f);
    CHECK (gainAt (f, 100.0f) < -25.0f);
    CHECK (gainAt (f, 10000.0f) < -25.0f);
    m.lpRes = 1.0f; m.bpOn = false; m.lpHz = 2000.0f; f.reset(); f.setParams (m);
    CHECK (gainAt (f, 2000.0f) > 15.0f);   // resonance peak
}

TEST_CASE ("FX: chains - off = passthrough/zero CPU, click-free switching, sleep after tail", "[fx]")
{
    LayerFxChain chain;
    chain.prepare (fs);
    LayerFxParams p;
    chain.setParams (p);
    REQUIRE_FALSE (chain.anyOn());
    auto s = sine (440.0f, 0.5f, 4800);
    const auto dry = s;
    run (chain, s);
    REQUIRE (s.l == dry.l);

    // Everything on with a noisy input, then switch off: smooth transition, bounded.
    p.distOn = p.chorusOn = p.phaserOn = p.flangerOn = p.delayOn = p.reverbOn = p.eqOn = p.compOn = true;
    chain.setParams (p);
    Stereo x (48000 * 4);
    chroma::fx::Rng rng;
    for (size_t i = 0; i < 48000; ++i) x.l[i] = x.r[i] = 0.3f * std::sin (tpi * 220.0f * (float) i / (float) fs);
    for (size_t i = 0; i < x.l.size(); i += 32)
    {
        if (i == 48000) { chain.setParams (LayerFxParams {}); }
        chain.process (x.l.data() + i, x.r.data() + i, 32);
    }
    REQUIRE (finite (x, 8.0f));
    REQUIRE_FALSE (chain.anyOn());

    // On with silence: tail decays, then the chain sleeps.
    LayerFxParams q; q.delayOn = q.reverbOn = true; q.delay.feedback = 0.5f; q.reverb.decay = 0.5f;
    chain.setParams (q);
    Stereo y (48000 * 12);
    y.l[100] = y.r[100] = 1.0f;
    run (chain, y);
    CHECK (chain.isAsleep());

    MasterFxChain master;
    master.prepare (fs);
    MasterFxParams mp;
    master.setParams (mp);
    REQUIRE_FALSE (master.anyOn());
    mp.driveOn = mp.chorusOn = mp.delayOn = mp.reverbOn = mp.filterOn = true;
    for (int t = 0; t < 6; ++t)
    {
        mp.drive.type = masterDriveType (t);
        mp.delay.type = (DelayType) t;
        mp.chorus.mode = (ChorusMode) t;
        mp.reverb.type = (ReverbType) (t % 4);
        master.setParams (mp);
        auto z = sine (330.0f, 0.5f, 24000);
        master.process (z.l.data(), z.r.data(), (int) z.l.size());
        INFO ("variant " << t);
        REQUIRE (finite (z, 8.0f));
    }
}

TEST_CASE ("FX: CPU of 12 full layer chains + master", "[fx]")
{
    std::vector<LayerFxChain> chains (12);
    LayerFxParams p;
    p.distOn = p.chorusOn = p.phaserOn = p.flangerOn = p.delayOn = p.reverbOn = p.eqOn = p.compOn = true;
    for (auto& c : chains) { c.prepare (fs); c.setParams (p); }
    MasterFxChain m;
    m.prepare (fs);
    MasterFxParams mp; mp.driveOn = mp.chorusOn = mp.delayOn = mp.reverbOn = mp.filterOn = true;
    m.setParams (mp);
    auto s = sine (220.0f, 0.3f, 48000);
    std::vector<float> l (32), r (32);
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < s.l.size(); i += 32)
    {
        for (auto& c : chains)
        {
            std::copy (s.l.begin() + (long) i, s.l.begin() + (long) i + 32, l.begin());
            std::copy (s.r.begin() + (long) i, s.r.begin() + (long) i + 32, r.begin());
            c.process (l.data(), r.data(), 32);
        }
        m.process (l.data(), r.data(), 32);
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    WARN ("12 layer FX chains (all 8 FX) + master FX, 1 s @ 48 kHz: " << secs * 100.0 << "% of one core");
    CHECK (secs < 1.0);
}

TEST_CASE ("FX: integration through the synth (layer insert + master)", "[fx]")
{
    REQUIRE (lp::descs[(size_t) lp::fxDelayMix].modDest);
    REQUIRE (lp::descs[(size_t) lp::fxReverbMix].modDest);

    // Layer 1: short note into a 100 ms delay -> echoes after the note has ended.
    test::Rig rig;
    auto& st = rig.synth().layerStore (0);
    st.set (lp::fxDelayOn, 1.0f);
    st.set (lp::fxDelayTimeL, 100.0f);
    st.set (lp::fxDelayTimeR, 100.0f);
    st.set (lp::fxDelayMix, 0.5f);
    st.set (lp::fxDelayFeedback, 0.5f);
    st.set (lp::env1Release, 0.001f);
    rig.noteOn (60);
    rig.run ({}, 10);
    rig.noteOff (60);
    rig.run ({}, 20);   // voice gone
    float tail = 0.0f;
    for (int b = 0; b < 150; ++b)
    {
        rig.run();
        tail = std::max (tail, rig.buffer.getMagnitude (0, 0, 64));
    }
    CHECK (tail > 0.01f);

    // Master Reverb on via host parameter.
    test::Rig rig2;
    test::setParam (rig2.proc, chroma::ParamIDs::mfxReverbOn, 1.0f);
    test::setParam (rig2.proc, chroma::ParamIDs::mfxReverbMix, 0.5f);
    rig2.synth().layerStore (0).set (lp::env1Release, 0.001f);
    rig2.noteOn (60);
    rig2.run ({}, 10);
    rig2.noteOff (60);
    rig2.run ({}, 20);
    float rtail = 0.0f;
    for (int b = 0; b < 100; ++b)
    {
        rig2.run();
        rtail = std::max (rtail, rig2.buffer.getMagnitude (0, 0, 64));
    }
    CHECK (rtail > 0.001f);
}

TEST_CASE ("FX: per-module CPU", "[.bench]")
{
    auto bench = [] (auto& fx, const char* name)
    {
        auto s = sine (220.0f, 0.3f, 480000);
        const auto t0 = std::chrono::steady_clock::now();
        run (fx, s);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        WARN (name << ": " << secs * 10.0 << "% of one core");
    };
    Distortion d; d.prepare (fs); d.setParams ({}); bench (d, "distortion");
    Chorus c; c.prepare (fs); c.setParams ({}); bench (c, "chorus");
    Phaser p; p.prepare (fs); p.setParams ({}); bench (p, "phaser");
    Flanger f; f.prepare (fs); f.setParams ({}); bench (f, "flanger");
    Delay y; y.prepare (fs); y.setParams ({}); bench (y, "delay");
    Reverb r; r.prepare (fs); r.setParams ({}); bench (r, "reverb fdn");
    ReverbParams pp; pp.type = ReverbType::Plate; Reverb r2; r2.prepare (fs); r2.setParams (pp); bench (r2, "reverb plate");
    Eq3 e; e.prepare (fs); e.setParams ({}); bench (e, "eq");
    Compressor k; k.prepare (fs); k.setParams ({}); bench (k, "comp");
    MasterFilter m; m.prepare (fs); m.setParams ({}); bench (m, "master filter");
}
