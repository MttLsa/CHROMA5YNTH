#pragma once
// GLOBAL SCALE: real-time, continuous pitch correction of one synth's (layer's) output towards the notes of a
// scale. Same audio engine and pitch recognition as Groovulation's MELODIC modules: Signalsmith Stretch
// (polyphonic phase vocoder) + YIN fundamental tracking. Two modes:
//  - Poly: custom frequency map, every spectral peak (exact frequency from its phase advance) is moved towards
//    the nearest scale pitch: works on chords and inharmonic sounds.
//  - Mono: the fundamental is recognised in real time (YIN on a 12 kHz mono copy, 10.7 ms hop) and the whole
//    spectrum is shifted by the same ratio: harmonic timbre preserved exactly (leads, basses).
// Both use a smooth (C1) staircase, so pitch sweeps from automation glide continuously from note to note
// instead of jumping. Latency = Stretch block (≈ 64 ms, reported to the host).
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
        decim = std::max (1, (int) std::lround (sampleRate / 12000.0));
        yinRate = sampleRate / decim;
        lpCoef = 1.0f - std::exp (-2.0f * 3.14159265f * 2500.0f / (float) sampleRate);
        latency = stretch.inputLatency() + stretch.outputLatency();
        reset();
    }
    void reset()
    {
        stretch.reset();
        warm = false;
        ring.fill (0.0f);
        ringPos = 0; decimCount = 0; hopCount = 0; lp1 = lp2 = 0.0f; acc = 0.0f;
        f0 = 0.0f; ratio = 1.0f; ratioTarget = 1.0f;
    }
    float detectedHz() const noexcept { return f0; }   // Mono mode: last recognised fundamental (0 = unvoiced)
    int getLatency() const noexcept { return latency; }

    // Called once per block (audio thread) before process().
    void setScale (int root, int scaleIndex, float amount01, float smooth01, float refA4 = 440.0f, bool monoMode = false) noexcept
    {
        mono = monoMode;
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
        if (mono)
            analyse (l, r, n);
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
    // ---- YIN fundamental tracking (Mono mode)
    static constexpr int win = 512, maxTau = 300, minTau = 3, hop = 128, ringSize = 1024;
    void analyse (const float* l, const float* r, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float x = 0.5f * (l[i] + r[i]);
            lp1 += lpCoef * (x - lp1);
            lp2 += lpCoef * (lp1 - lp2);
            if (++decimCount < decim) continue;
            decimCount = 0;
            ring[(size_t) ringPos] = lp2;
            ringPos = (ringPos + 1) & (ringSize - 1);
            if (++hopCount >= hop) { hopCount = 0; runYin(); }
        }
        ratio += 0.25f * (ratioTarget - ratio);   // de-zipper between hops
    }
    void runYin() noexcept
    {
        // last win + maxTau samples, oldest first
        const int len = win + maxTau;
        float energy = 0.0f;
        for (int j = 0; j < len; ++j)
        {
            buf[(size_t) j] = ring[(size_t) ((ringPos - len + j + ringSize) & (ringSize - 1))];
            energy += buf[(size_t) j] * buf[(size_t) j];
        }
        if (energy / (float) len < 1.0e-7f) { f0 = 0.0f; ratioTarget = 1.0f; return; }
        float running = 0.0f;
        int best = -1;
        cmnd[0] = 1.0f;
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            float d = 0.0f;
            for (int j = 0; j < win; ++j)
            {
                const float e = buf[(size_t) j] - buf[(size_t) (j + tau)];
                d += e * e;
            }
            running += d;
            cmnd[(size_t) tau] = running > 0.0f ? d * (float) tau / running : 1.0f;
            if (best < 0 && tau > minTau + 1 && cmnd[(size_t) tau - 1] < 0.15f && cmnd[(size_t) tau - 1] <= cmnd[(size_t) tau])
                best = tau - 1;
        }
        if (best < 0)
        {
            int m = minTau;
            for (int tau = minTau; tau <= maxTau; ++tau) if (cmnd[(size_t) tau] < cmnd[(size_t) m]) m = tau;
            if (cmnd[(size_t) m] > 0.3f) { f0 = 0.0f; ratioTarget = 1.0f; return; }   // unvoiced: leave the sound alone
            best = m;
        }
        float t = (float) best;
        if (best > 1 && best < maxTau)
        {
            const float a = cmnd[(size_t) best - 1], b = cmnd[(size_t) best], c = cmnd[(size_t) best + 1];
            const float den = a - 2.0f * b + c;
            if (den > 0.0f) t += 0.5f * (a - c) / den;
        }
        f0 = (float) yinRate / t;
        const float p = 69.0f + 12.0f * std::log2 (f0 / a4);
        ratioTarget = std::exp2 ((snapPitch (p) - p) / 12.0f);
    }

    float mapFreq (float f) const noexcept     // f in cycles/sample
    {
        if (mono) return f * ratio;
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
    bool mono = false;
    std::array<float, ringSize> ring {};
    std::array<float, win + maxTau> buf {};
    std::array<float, maxTau + 1> cmnd {};
    int ringPos = 0, decim = 4, decimCount = 0, hopCount = 0;
    double yinRate = 12000.0;
    float lpCoef = 0.3f, lp1 = 0.0f, lp2 = 0.0f, acc = 0.0f, f0 = 0.0f, ratio = 1.0f, ratioTarget = 1.0f;
    double sr = 48000.0;
    int latency = 0;
    bool warm = false;
};
} // namespace chroma
