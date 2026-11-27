#pragma once
// GLOBAL SCALE: real-time, continuous pitch correction of one synth's (layer's) output towards the notes of a
// scale. Same audio engine as Groovulation's MELODY modules (Signalsmith Stretch, polyphonic phase vocoder),
// driven with a custom frequency map: every spectral peak is moved towards the nearest scale pitch with a
// smooth (C1) staircase, so pitch sweeps from automation glide continuously from note to note instead of
// jumping. Works on chords too (each partial is mapped on its own). Latency = Stretch block (≈ 64 ms).
#include "Scales.h"
#include "../../third_party/signalsmith-stretch/signalsmith-stretch.h"
#include <array>
#include <cmath>
#include <vector>

namespace chroma
{
class ScaleCorrector
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        // 64 ms analysis block, 8× overlap: ≈ 1 cent accuracy on a steady tone (measured: 452 Hz -> 440.25 Hz),
        // latency = one block (≈ 64 ms, reported to the host).
        const int block = (int) std::lround (sampleRate * 0.06375), interval = block / 8;
        stretch.configure (2, block, interval, false);
        stretch.setFreqMap ([this] (float f) { return mapFreq (f); });
        inL.assign ((size_t) maxBlock, 0.0f);
        inR.assign ((size_t) maxBlock, 0.0f);
        latency = stretch.inputLatency() + stretch.outputLatency();
        reset();
    }
    void reset() { stretch.reset(); warm = false; }
    int getLatency() const noexcept { return latency; }

    // Called once per block (audio thread) before process().
    void setScale (int root, int scaleIndex, float amount01, float smooth01, float refA4 = 440.0f) noexcept
    {
        const auto m = scales::mask (root, scaleIndex);
        for (int k = 0; k < 12; ++k)
        {
            int lo = 0;
            while (lo < 12 && ! m[(size_t) (((k - lo) % 12 + 12) % 12)]) ++lo;
            int up = 1;
            while (up <= 12 && ! m[(size_t) ((k + up) % 12)]) ++up;
            lower[(size_t) k] = lo;
            upper[(size_t) k] = up;
        }
        amount = std::clamp (amount01, 0.0f, 1.0f);
        width = 0.9f * std::clamp (smooth01, 0.0f, 1.0f);
        a4 = refA4;
    }

    // In-place stereo processing; n ≤ maxBlock. The dry signal is not mixed: output is the corrected signal,
    // delayed by getLatency() (the processor reports it to the host).
    void process (float* l, float* r, int n) noexcept
    {
        std::copy (l, l + n, inL.begin());
        std::copy (r, r + n, inR.begin());
        const float* ins[2] { inL.data(), inR.data() };
        float* outs[2] { l, r };
        stretch.process (ins, n, outs, n);
        warm = true;
    }

    // MIDI pitch (fractional) -> corrected pitch.
    float snapPitch (float p) const noexcept
    {
        if (amount <= 0.0f) return p;
        const float fl = std::floor (p);
        const int k = (((int) fl) % 12 + 12) % 12;
        const float a = fl - (float) lower[(size_t) k];
        const float b = fl + (float) upper[(size_t) k];
        float x = (p - a) / (b - a);   // 0..1 between two neighbouring scale notes
        float s;
        if (width <= 1.0e-4f) s = x < 0.5f ? 0.0f : 1.0f;
        else
        {
            const float t = std::clamp ((x - 0.5f) / width + 0.5f, 0.0f, 1.0f);
            s = t * t * (3.0f - 2.0f * t);
        }
        const float target = a + (b - a) * s;
        return p + amount * (target - p);
    }

private:
    float mapFreq (float f) const noexcept     // f in cycles/sample
    {
        const float hz = f * (float) sr;
        if (hz < 20.0f || amount <= 0.0f) return f;
        const float p = 69.0f + 12.0f * std::log2 (hz / a4);
        const float q = snapPitch (p);
        return f * std::exp2 ((q - p) / 12.0f);
    }

    signalsmith::stretch::SignalsmithStretch<float, void> stretch;
    std::vector<float> inL, inR;
    std::array<int, 12> lower {}, upper {};
    float amount = 1.0f, width = 0.3f, a4 = 440.0f;
    double sr = 48000.0;
    int latency = 0;
    bool warm = false;
};
} // namespace chroma
