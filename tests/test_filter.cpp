#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "TestHelpers.h"
#include "shared/Drift.h"
#include "shared/Filter.h"
#include <cmath>
#include <vector>

using namespace test;
using chroma::Filter;
using chroma::FilterModel;

namespace
{
constexpr double twoPi = 6.283185307179586;
constexpr int numModels = (int) FilterModel::count;
const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };

void run (Filter& f, std::vector<float>& l, std::vector<float>& r, float fc0, float fc1 = -1.0f)
{
    if (fc1 < 0.0f) fc1 = fc0;
    const int n = (int) l.size();
    for (int i = 0; i < n; i += 32)
    {
        const int m = std::min (32, n - i);
        const float a = fc0 * std::pow (fc1 / fc0, (float) i / (float) n);
        const float b = fc0 * std::pow (fc1 / fc0, (float) (i + m) / (float) n);
        f.process (l.data() + i, r.data() + i, m, a, b);
    }
}

// Linear-regime gain (dB) at freq, measured by projecting the settled output on the test sine.
double gainDb (FilterModel m, double sr, float fc, double freq, float res = 0.0f)
{
    Filter f;
    f.prepare (sr);
    f.setModel (m);
    f.setResonance (res);
    const int n = (int) (sr * 0.5);
    const double amp = 0.02;
    std::vector<float> l ((size_t) n), r ((size_t) n);
    for (int i = 0; i < n; ++i)
        l[(size_t) i] = r[(size_t) i] = (float) (amp * std::sin (twoPi * freq * i / sr));
    run (f, l, r, fc);
    const int periods = std::max (1, (int) (freq * 0.2));
    const int len = (int) std::lround (periods * sr / freq);
    double re = 0, im = 0;
    for (int i = n - len; i < n; ++i)
    {
        re += l[(size_t) i] * std::sin (twoPi * freq * i / sr);
        im += l[(size_t) i] * std::cos (twoPi * freq * i / sr);
    }
    const double mag = 2.0 * std::sqrt (re * re + im * im) / len;
    return 20.0 * std::log10 (std::max (mag / amp, 1.0e-12));
}

bool isLowpass (FilterModel m)
{
    switch (m)
    {
        case FilterModel::LadderLP24: case FilterModel::DiodeLP: case FilterModel::SvfLP12: case FilterModel::OtaLP24:
        case FilterModel::CascadeLP24: case FilterModel::SkLP12: case FilterModel::BridgeLP12: case FilterModel::ClassicLP24:
        case FilterModel::DigLP12: case FilterModel::DigLP24: case FilterModel::DigLP36: case FilterModel::DigLP48:
            return true;
        default: return false;
    }
}
int poles (FilterModel m)
{
    switch (m)
    {
        case FilterModel::SvfLP12: case FilterModel::SkLP12: case FilterModel::BridgeLP12: case FilterModel::DigLP12:
        case FilterModel::SvfHP12: case FilterModel::SkHP12: case FilterModel::DigHP12: return 2;
        case FilterModel::DigLP36: return 6;
        case FilterModel::DigLP48: return 8;
        default: return 4;
    }
}
} // namespace

TEST_CASE ("Filter: lowpass frequency response", "[filter]")
{
    const double sr = GENERATE (44100.0, 96000.0);
    for (int i = 0; i < numModels; ++i)
    {
        const auto m = (FilterModel) i;
        if (! isLowpass (m)) continue;
        INFO ("model " << i << " sr " << sr);
        const double pass = gainDb (m, sr, 1000.0f, 100.0);
        CHECK (std::fabs (pass) < 3.0);
        // stop band: at least ~4.5 dB/oct per pole two octaves above cutoff
        CHECK (gainDb (m, sr, 1000.0f, 4000.0) < pass - 4.5 * poles (m) * 2.0 + 3.0);
    }
}

TEST_CASE ("Filter: cutoff accuracy at res 0", "[filter]")
{
    for (double sr : rates)
    {
        INFO ("sr " << sr);
        CHECK (std::fabs (gainDb (FilterModel::LadderLP24, sr, 2000.0f, 2000.0) + 12.04) < 1.0);   // 4 x -3 dB
        CHECK (std::fabs (gainDb (FilterModel::SvfLP12, sr, 2000.0f, 2000.0) + 6.02) < 1.0);      // Q 0.5
        CHECK (std::fabs (gainDb (FilterModel::SkLP12, sr, 2000.0f, 2000.0) + 6.02) < 1.0);
        CHECK (std::fabs (gainDb (FilterModel::DigLP12, sr, 2000.0f, 2000.0) + 3.01) < 0.5);      // Butterworth
        CHECK (std::fabs (gainDb (FilterModel::DigLP48, sr, 2000.0f, 2000.0) + 3.01) < 0.5);
        CHECK (std::fabs (gainDb (FilterModel::DigHP24, sr, 2000.0f, 2000.0) + 3.01) < 0.5);
    }
}

TEST_CASE ("Filter: highpass, bandpass, notch, comb", "[filter]")
{
    const double sr = 48000.0;
    for (auto m : { FilterModel::SvfHP12, FilterModel::SkHP12, FilterModel::DigHP12, FilterModel::DigHP24 })
    {
        INFO ("model " << (int) m);
        const double pass = gainDb (m, sr, 500.0f, 8000.0);
        CHECK (std::fabs (pass) < 3.0);
        CHECK (gainDb (m, sr, 500.0f, 125.0) < pass - 4.5 * poles (m) * 2.0 + 3.0);
    }
    for (auto m : { FilterModel::SvfBP12, FilterModel::DigBP })
    {
        INFO ("model " << (int) m);
        const double peak = gainDb (m, sr, 1000.0f, 1000.0, 0.5f);
        CHECK (peak > gainDb (m, sr, 1000.0f, 125.0, 0.5f) + 12.0);
        CHECK (peak > gainDb (m, sr, 1000.0f, 8000.0, 0.5f) + 12.0);
    }
    for (auto m : { FilterModel::SvfNotch, FilterModel::DigNotch })
    {
        INFO ("model " << (int) m);
        CHECK (gainDb (m, sr, 1000.0f, 1000.0) < -20.0);
        CHECK (gainDb (m, sr, 1000.0f, 50.0) > -3.0);
    }
    // Comb +: peaks at multiples of the comb frequency, Comb -: notches there
    CHECK (gainDb (FilterModel::CombPlus, sr, 400.0f, 800.0, 0.8f) > gainDb (FilterModel::CombPlus, sr, 400.0f, 600.0, 0.8f) + 12.0);
    CHECK (gainDb (FilterModel::CombMinus, sr, 400.0f, 600.0, 0.8f) > gainDb (FilterModel::CombMinus, sr, 400.0f, 800.0, 0.8f) + 12.0);
    // Formant: vowel A (cutoff ~20 Hz) emphasises F1 730 Hz over 5 kHz
    CHECK (gainDb (FilterModel::Formant, sr, 20.0f, 730.0, 0.5f) > gainDb (FilterModel::Formant, sr, 20.0f, 5000.0, 0.5f) + 12.0);
}

TEST_CASE ("Filter: analog models self-oscillate, stable and in tune at max resonance", "[filter]")
{
    for (double sr : rates)
        for (int i = 0; i <= (int) FilterModel::ClassicLP24; ++i)
        {
            const auto m = (FilterModel) i;
            INFO ("model " << i << " sr " << sr);
            Filter f;
            f.prepare (sr);
            f.setModel (m);
            f.setResonance (1.0f);
            std::vector<float> l ((size_t) (sr * 2.0), 0.0f), r = l;
            run (f, l, r, 1000.0f);
            float peak = 0.0f;
            bool finite = true;
            for (float v : l) { finite = finite && std::isfinite (v); peak = std::max (peak, std::fabs (v)); }
            REQUIRE (finite);
            CHECK (peak < 4.0f);

            // last 0.5 s: sustained oscillation near the cutoff (HP/notch outputs oscillate too)
            const size_t from = l.size() - (size_t) (sr * 0.5);
            double e = 0.0;
            int crossings = 0;
            for (size_t k = from; k < l.size(); ++k)
            {
                e += (double) l[k] * l[k];
                if (k > from && (l[k - 1] < 0.0f) != (l[k] < 0.0f)) ++crossings;
            }
            const double rms = std::sqrt (e / (double) (l.size() - from));
            CHECK (rms > 1.0e-3);
            const double hz = crossings / 2.0 / 0.5;
            CHECK (hz > 900.0);
            CHECK (hz < 1100.0);
        }
}

TEST_CASE ("Filter: bounded with hot input, max drive/resonance and fast sweeps", "[filter]")
{
    uint32_t seed = 1;
    for (double sr : rates)
        for (int i = 0; i < numModels; ++i)
        {
            INFO ("model " << i << " sr " << sr);
            Filter f;
            f.prepare (sr);
            f.setModel ((FilterModel) i);
            f.setResonance (1.0f);
            f.setDrive (1.0f);
            std::vector<float> l ((size_t) (sr * 0.5)), r ((size_t) (sr * 0.5));
            for (size_t k = 0; k < l.size(); ++k)
            {
                seed = seed * 1664525u + 1013904223u;
                l[k] = r[k] = ((float) (seed >> 8) / 8388608.0f - 1.0f) * 2.0f;   // ±2 noise
            }
            run (f, l, r, 20.0f, 20000.0f);
            float peak = 0.0f;
            bool finite = true;
            for (size_t k = 0; k < l.size(); ++k)
            {
                finite = finite && std::isfinite (l[k]) && std::isfinite (r[k]);
                peak = std::max (peak, std::fabs (l[k]));
            }
            REQUIRE (finite);
            CHECK (peak < 10.0f);
        }
}

TEST_CASE ("Filter: no denormals after the input stops", "[filter]")
{
    for (int i = 0; i < numModels; ++i)
    {
        INFO ("model " << i);
        Filter f;
        f.prepare (48000.0);
        f.setModel ((FilterModel) i);
        f.setResonance (0.3f);
        std::vector<float> l (48000 * 6, 0.0f), r = l;
        for (int k = 0; k < 4800; ++k)
            l[(size_t) k] = r[(size_t) k] = (float) std::sin (k * 0.05);
        run (f, l, r, 200.0f);
        int subnormals = 0;
        for (float v : l) subnormals += std::fpclassify (v) == FP_SUBNORMAL ? 1 : 0;
        CHECK (subnormals == 0);
    }
}

TEST_CASE ("Drift: bounded, slow and deterministic", "[filter][drift]")
{
    chroma::Drift a, b;
    a.start (42); b.start (42);
    float maxSemis = 0.0f, maxStep = 0.0f, prev = 0.0f;
    for (int t = 0; t < 2000; ++t)
    {
        a.tick (1.0f, 0.01f); b.tick (1.0f, 0.01f);
        const float v = a.pitchSemis()[3];
        REQUIRE (v == b.pitchSemis()[3]);
        if (t > 0) maxStep = std::max (maxStep, std::fabs (v - prev));
        prev = v;
        maxSemis = std::max (maxSemis, std::fabs (v));
        CHECK (std::fabs (a.cutoffOctaves()) <= chroma::Drift::maxCutoffOct);
    }
    CHECK (maxSemis <= chroma::Drift::maxCents * 0.01f);
    CHECK (maxSemis > 0.01f);
    CHECK (maxStep < 0.01f);   // < 1 cent per 10 ms
    REQUIRE (a.pitchSemis()[0] != a.pitchSemis()[1]);   // independent per oscillator
}

TEST_CASE ("Voice filter: lowpass darkens the layer, Drift Only bypasses it", "[filter]")
{
    auto energy = [] (bool filterOn, bool driftOnly)
    {
        Rig r;
        auto& s = r.synth().layerStore (0);
        s.set (chroma::lp::filterOn, filterOn ? 1.0f : 0.0f);
        s.set (chroma::lp::filterModel, (float) FilterModel::LadderLP24);
        s.set (chroma::lp::filterCutoff, 100.0f);
        s.set (chroma::lp::driftOn, driftOnly ? 1.0f : 0.0f);
        s.set (chroma::lp::driftOnly, driftOnly ? 1.0f : 0.0f);
        r.run ({}, 20);   // let smoothing settle
        r.noteOn (84);
        double e = 0.0;
        for (int b = 0; b < 100; ++b)
        {
            r.run();
            for (int i = 0; i < r.buffer.getNumSamples(); ++i)
            {
                const float v = r.buffer.getSample (0, i);
                REQUIRE (std::isfinite (v));
                e += (double) v * v;
            }
        }
        return e;
    };
    const double dry = energy (false, false);
    REQUIRE (dry > 0.0);
    CHECK (energy (true, false) < dry * 0.01);          // C6 through a 100 Hz LP24
    CHECK (energy (true, true) > dry * 0.5);            // Drift Only: filter bypassed
}
