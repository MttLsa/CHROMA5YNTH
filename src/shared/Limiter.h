#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace chroma
{
// Master true-peak limiter (§4): look-ahead, 4x oversampled peak detection, fixed ceiling -1 dBTP.
// Gain law: required gain per sample (covering inter-sample peaks on both sides) -> sliding minimum
// over the look-ahead window -> release -> moving average over the same window. The average of
// values that are each <= the requirement of the output sample guarantees no overshoot.
class TruePeakLimiter
{
public:
    static constexpr float ceilingDb = -1.0f;
    static constexpr int halfTaps = 32;         // interpolator half length (64 taps per phase)

    void prepare (double sampleRate, int numChannels)
    {
        channels = std::clamp (numChannels, 1, 2);
        lookahead = std::clamp ((int) std::lround (0.0015 * sampleRate), 8, maxLookahead);
        releaseCoeff = (float) std::exp (-1.0 / (0.08 * sampleRate));
        buildKernel();
        reset();
    }

    void reset()
    {
        for (auto& r : ring) r.fill (0.0f);
        ispRing.fill (0.0f);
        minIdx.fill (0); minVal.fill (1.0f);
        dqHead = dqTail = 0;
        n = 0; released = 1.0f; boxSum = (double) lookahead;
        boxRing.fill (1.0f);
        lastGain = 1.0f;
    }

    int getLatencySamples() const noexcept { return 2 * halfTaps + lookahead - 1; }
    float getGainReduction() const noexcept { return lastGain; }

    // Processes in place. `data` holds `channels` pointers (only `channels` are used).
    void process (float* const* data, int numSamples) noexcept
    {
        const float ceiling = std::pow (10.0f, (ceilingDb - safetyDb) / 20.0f);
        const float hardCeil = std::pow (10.0f, ceilingDb / 20.0f);

        for (int s = 0; s < numSamples; ++s)
        {
            // 1. push input
            for (int ch = 0; ch < channels; ++ch)
                ring[(size_t) ch][(size_t) (n & mask)] = data[ch][s];

            // 2. inter-sample peak of interval (k-1, k], k = n - (halfTaps - 1)
            const std::int64_t k = n - (halfTaps - 1);
            float isp = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
            {
                const auto& x = ring[(size_t) ch];
                isp = std::max (isp, std::fabs (x[(size_t) (k & mask)]));
                for (int ph = 0; ph < 3; ++ph)
                {
                    float acc = 0.0f;
                    for (int i = 0; i < 2 * halfTaps; ++i)
                        acc += x[(size_t) ((k - 1 - (halfTaps - 1) + i) & mask)] * kernel[(size_t) ph][(size_t) i];
                    isp = std::max (isp, std::fabs (acc));
                }
            }
            ispRing[(size_t) (k & mask)] = isp;

            // 3. requirement for sample j = k - 1 (intervals on both sides)
            const std::int64_t j = k - 1;
            const float peak = std::max (ispRing[(size_t) (j & mask)], isp);
            const float req = peak > ceiling ? ceiling / peak : 1.0f;

            // 4. sliding minimum over look-ahead + interpolator support (gain is then <= the
            //    requirement of every sample the reconstruction of the output sample depends on)
            while (dqTail != dqHead && minVal[(size_t) ((dqTail - 1) & dqMask)] >= req) --dqTail;
            minIdx[(size_t) (dqTail & dqMask)] = j; minVal[(size_t) (dqTail & dqMask)] = req; ++dqTail;
            while (minIdx[(size_t) (dqHead & dqMask)] <= j - lookahead - 2 * halfTaps) ++dqHead;
            const float m = minVal[(size_t) (dqHead & dqMask)];

            // 5. release (never above the windowed minimum)
            released = std::min (m, 1.0f - (1.0f - released) * releaseCoeff);

            // 6. moving average over the same window
            boxSum += (double) released - (double) boxRing[(size_t) ((j - lookahead) & boxMask)];
            boxRing[(size_t) (j & boxMask)] = released;
            float g = (float) (boxSum / (double) lookahead);
            g = std::min (g, 1.0f);
            lastGain = g;

            // 7. output the delayed sample c = j - lookahead - halfTaps + 1
            const std::int64_t c = j - lookahead - halfTaps + 1;
            for (int ch = 0; ch < channels; ++ch)
            {
                const float y = ring[(size_t) ch][(size_t) (c & mask)] * g;
                data[ch][s] = std::clamp (y, -hardCeil, hardCeil);
            }
            ++n;
        }
    }

private:
    void buildKernel()
    {
        for (int ph = 0; ph < 3; ++ph)
        {
            const double f = 0.25 * (ph + 1);
            for (int i = 0; i < 2 * halfTaps; ++i)
            {
                const double t = f - (double) (i - (halfTaps - 1));   // distance from tap to target point
                const double sinc = std::abs (t) < 1e-9 ? 1.0 : std::sin (3.14159265358979323846 * t) / (3.14159265358979323846 * t);
                const double a = 3.14159265358979323846 * t / (double) halfTaps;
                const double w = 0.5 + 0.5 * std::cos (a);   // Hann
                kernel[(size_t) ph][(size_t) i] = (float) (sinc * w);
            }
        }
    }

    static constexpr int ringSize = 4096, mask = ringSize - 1;
    static constexpr int maxLookahead = 1024;
    static constexpr int dqSize = 2048, dqMask = dqSize - 1;
    static constexpr int boxSize = 2048, boxMask = boxSize - 1;
    // Worst-case gap between 4x-oversampled points and the continuous peak is cos(pi/8) = -0.69 dB.
    static constexpr float safetyDb = 0.7f;

    int channels = 2, lookahead = 64;
    float releaseCoeff = 0.999f;
    std::array<std::array<float, 2 * halfTaps>, 3> kernel {};
    std::array<std::array<float, ringSize>, 2> ring {};
    std::array<float, ringSize> ispRing {};
    std::array<std::int64_t, dqSize> minIdx {};
    std::array<float, dqSize> minVal {};
    std::int64_t dqHead = 0, dqTail = 0;
    std::array<float, boxSize> boxRing {};
    std::int64_t n = 0;
    float released = 1.0f, lastGain = 1.0f;
    double boxSum = 0.0;
};
} // namespace chroma
