#include <catch2/catch_test_macros.hpp>
#include "TestHelpers.h"
#include "shared/Limiter.h"
#include <cmath>
#include <random>
#include <vector>

namespace
{
constexpr double pi = 3.14159265358979323846;
}
namespace
{
// Independent true-peak meter: 16x oversampling with a long windowed-sinc interpolator.
float truePeakDb (const std::vector<float>& x, size_t skip)
{
    constexpr int os = 16, half = 64;
    float peak = 0.0f;
    for (size_t n = skip + half; n + half < x.size(); ++n)
        for (int k = 0; k < os; ++k)
        {
            const double f = (double) k / os;
            double acc = 0.0;
            for (int i = -half + 1; i <= half; ++i)
            {
                const double t = f - i;
                const double sinc = std::abs (t) < 1e-12 ? 1.0 : std::sin (pi * t) / (pi * t);
                const double w = 0.42 + 0.5 * std::cos (pi * t / half) + 0.08 * std::cos (2 * pi * t / half);
                acc += x[(size_t) ((long) n + i)] * sinc * w;
            }
            peak = std::max (peak, (float) std::abs (acc));
        }
    return 20.0f * std::log10 (std::max (peak, 1e-9f));
}

std::vector<std::vector<float>> runLimiter (std::vector<float> l, std::vector<float> r, int block = 256)
{
    chroma::TruePeakLimiter lim;
    lim.prepare (48000.0, 2);
    for (size_t p = 0; p < l.size(); p += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, l.size() - p);
        float* ch[] { l.data() + p, r.data() + p };
        lim.process (ch, n);
    }
    return { l, r };
}
} // namespace

TEST_CASE ("Limiter never exceeds -1 dBTP", "[limiter]")
{
    constexpr size_t len = 24000;
    std::mt19937 rng (1);
    std::normal_distribution<float> noise (0.0f, 1.5f);

    std::vector<std::vector<float>> signals;
    { std::vector<float> s (len); for (size_t i = 0; i < len; ++i) s[i] = 2.0f * std::sin ((float) pi * 0.5f * (float) i + 0.785398f); signals.push_back (s); }
    { std::vector<float> s (len); for (auto& v : s) v = noise (rng); signals.push_back (s); }
    { std::vector<float> s (len); for (size_t i = 0; i < len; ++i) s[i] = ((i / 3) % 2 ? 3.0f : -3.0f) * (i % 4000 < 2000 ? 1.0f : 0.1f); signals.push_back (s); }
    { std::vector<float> s (len); for (size_t i = 0; i < len; ++i) s[i] = 4.0f * std::sin (2.0f * (float) pi * 19000.0f * (float) i / 48000.0f + 1.0f) * (i % 1200 < 30 ? 1.0f : 0.05f); signals.push_back (s); }
    { std::vector<float> s (len, 0.0f); for (size_t i = 1000; i < len; i += 997) s[i] = (i & 1) ? 8.0f : -8.0f; signals.push_back (s); }

    for (auto& s : signals)
    {
        auto right = s;
        std::reverse (right.begin(), right.end());
        const auto out = runLimiter (s, right);
        INFO ("signal " << (&s - signals.data()));
        REQUIRE (truePeakDb (out[0], 0) <= -1.0f);
        REQUIRE (truePeakDb (out[1], 0) <= -1.0f);
    }
}

TEST_CASE ("Limiter is transparent below the ceiling", "[limiter]")
{
    constexpr size_t len = 4800;
    std::vector<float> s (len);
    for (size_t i = 0; i < len; ++i) s[i] = 0.25f * std::sin (0.05f * (float) i);
    chroma::TruePeakLimiter lim;
    lim.prepare (48000.0, 2);
    const int lat = lim.getLatencySamples();
    const auto out = runLimiter (s, s);
    for (size_t i = (size_t) lat; i < len; ++i)
        REQUIRE (std::abs (out[0][i] - s[i - (size_t) lat]) < 1e-5f);
}

TEST_CASE ("Synth output stays under -1 dBTP", "[limiter]")
{
    test::Rig r;
    test::setParam (r.proc, chroma::ParamIDs::masterVolume, 6.0f);
    for (size_t l = 1; l < 6; ++l)
    {
        test::setParam (r.proc, chroma::ParamIDs::layerOn[l], 1.0f);
        test::setParam (r.proc, chroma::ParamIDs::layerVolume[l], 6.0f);
    }
    juce::MidiBuffer m;
    for (int n = 48; n < 60; ++n) m.addEvent (juce::MidiMessage::noteOn (1, n, 1.0f), n);
    std::vector<float> outL;
    for (int b = 0; b < 300; ++b)
    {
        r.proc.processBlock (r.buffer, m);
        m.clear();
        outL.insert (outL.end(), r.buffer.getReadPointer (0), r.buffer.getReadPointer (0) + r.buffer.getNumSamples());
    }
    REQUIRE (truePeakDb (outL, 0) <= -1.0f);
    REQUIRE (truePeakDb (outL, 0) > -6.0f);   // actually limiting, not silent
}
