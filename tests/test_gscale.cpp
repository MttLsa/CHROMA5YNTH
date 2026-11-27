// GLOBAL SCALE (ScaleCorrector): smooth staircase mapping and real-time correction of a synth output.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "shared/ScaleCorrector.h"
#include "TestHelpers.h"
#include <cmath>
#include <vector>

using namespace chroma;

namespace
{
// Frequency of the strongest DFT bin (with parabolic interpolation) over x[from..from+len).
double peakHz (const std::vector<float>& x, size_t from, size_t len, double fs, double lo, double hi)
{
    double best = 0.0, bestF = 0.0;
    std::vector<double> mags;
    const double step = 0.25;
    for (double f = lo; f <= hi; f += step)
    {
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < len; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * (double) i / (double) (len - 1));
            const double ph = 2.0 * M_PI * f * (double) i / fs;
            re += w * x[from + i] * std::cos (ph);
            im -= w * x[from + i] * std::sin (ph);
        }
        const double m = re * re + im * im;
        if (m > best) { best = m; bestF = f; }
    }
    return bestF;
}

std::vector<float> correct (double hz, int root, int scale, float amount, float smooth, int seconds = 1)
{
    const double fs = 48000.0;
    ScaleCorrector c;
    c.prepare (fs, 256);
    c.setScale (root, scale, amount, smooth);
    std::vector<float> out;
    double ph = 0.0;
    std::vector<float> l (256), r (256);
    for (int b = 0; b < (int) (seconds * fs) / 256; ++b)
    {
        for (int i = 0; i < 256; ++i) { l[(size_t) i] = r[(size_t) i] = 0.5f * (float) std::sin (ph); ph += 2.0 * M_PI * hz / fs; }
        c.process (l.data(), r.data(), 256);
        out.insert (out.end(), l.begin(), l.end());
    }
    return out;
}
} // namespace

TEST_CASE ("Global Scale: smooth staircase mapping", "[gscale]")
{
    ScaleCorrector c;
    c.prepare (48000.0, 64);
    c.setScale (0, 0, 1.0f, 0.0f);                // C major, hard
    CHECK (c.snapPitch (60.0f) == Catch::Approx (60.0f));   // C stays
    CHECK (c.snapPitch (61.2f) == Catch::Approx (62.0f));   // C#+20 ct -> D
    CHECK (c.snapPitch (60.9f) == Catch::Approx (60.0f));   // between C and D, nearer C
    CHECK (c.snapPitch (66.4f) == Catch::Approx (67.0f));   // F#+40 ct -> G (70 % of the way F..G)
    CHECK (c.snapPitch (71.7f) == Catch::Approx (72.0f));   // B+70 -> C
    c.setScale (0, 0, 1.0f, 0.6f);                // smooth: continuous and monotonic
    float prev = c.snapPitch (55.0f);
    float maxJump = 0.0f;
    for (float p = 55.0f; p < 80.0f; p += 0.01f)
    {
        const float q = c.snapPitch (p);
        REQUIRE (q >= prev - 1.0e-4f);
        maxJump = std::max (maxJump, q - prev);
        prev = q;
    }
    CHECK (maxJump < 0.2f);                       // no step: a sweep glides from note to note
    CHECK (c.snapPitch (62.0f) == Catch::Approx (62.0f));   // scale notes are fixed points
    c.setScale (0, 0, 0.0f, 0.0f);                // Amount 0 = identity
    CHECK (c.snapPitch (61.37f) == Catch::Approx (61.37f));
    c.setScale (2, 33, 1.0f, 0.0f);               // Chromatic: nearest semitone
    CHECK (c.snapPitch (61.4f) == Catch::Approx (61.0f));
}

TEST_CASE ("Global Scale: an off-scale tone is pulled to the scale note", "[gscale]")
{
    // 452 Hz (A4 +47 ct, between A and A#): A minor pentatonic -> A 440 Hz
    const auto y = correct (452.0, 9, 11, 1.0f, 0.0f);
    const double f = peakHz (y, 24000, 16384, 48000.0, 400.0, 500.0);
    INFO ("corrected " << f);
    CHECK (std::abs (f - 440.0) < 1.5);
    // Amount 0: unchanged
    const auto z = correct (452.0, 9, 11, 0.0f, 0.0f);
    CHECK (std::abs (peakHz (z, 24000, 16384, 48000.0, 400.0, 500.0) - 452.0) < 1.5);
}

TEST_CASE ("Global Scale: synth integration, latency and zero cost when off", "[gscale]")
{
    test::Rig r;
    REQUIRE (r.synth().getLatencySamples() == r.proc.getLatencySamples());
    const int base = r.synth().getLatencySamples();
    test::setParam (r.proc, chroma::ParamIDs::gscaleOn, 1.0f);
    r.run();
    CHECK (r.synth().getLatencySamples() > base);
    r.noteOn (60, 0.9f);
    r.run ({}, 200);
    float peak = 0.0f;
    for (int i = 0; i < 64; ++i) peak = std::max (peak, std::abs (r.buffer.getSample (0, i)));
    CHECK (peak > 0.01f);
    test::setParam (r.proc, chroma::ParamIDs::gscaleOn, 0.0f);
    r.run();
    CHECK (r.synth().getLatencySamples() == base);
}
