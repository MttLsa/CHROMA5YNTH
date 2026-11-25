#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace chroma
{
// LFO (§2). Advanced at control rate.
struct LfoParams
{
    int shape = 0;            // Sine, Triangle, Square, Saw Up, Saw Down, S&H, Random Smooth, Custom
    float rateHz = 2.0f;
    bool sync = false;
    int division = 2;         // index into sync_divisions
    float phase = 0.0f;       // 0..1 cycles
    float offset = 0.0f;
    float delay = 0.0f, fadeIn = 0.0f, fadeOut = 0.0f;
    int mode = 1;             // Free, Retrigger, One-shot
    float smooth = 0.0f;
    float stereo = 0.0f;      // 0..0.5 cycles
    bool unipolar = false;
    float amount = 1.0f;
};

inline constexpr int lfoCustomPoints = 64;
using LfoCustomTable = std::array<std::atomic<float>, lfoCustomPoints>;

// Beats per LFO cycle for each sync division (1/1 = 4 beats; T = 2/3, D = 3/2).
inline float lfoDivisionBeats (int index) noexcept
{
    const int base = index % 6, kind = index / 6;
    const float beats = 4.0f / (float) (1 << base);
    return kind == 1 ? beats * 2.0f / 3.0f : kind == 2 ? beats * 1.5f : beats;
}

inline float lfoCyclesPerSecond (const LfoParams& p, double bpm) noexcept
{
    return p.sync ? (float) (bpm / 60.0) / lfoDivisionBeats (p.division) : p.rateHz;
}

class Lfo
{
public:
    void noteOn (const LfoParams& p, double freePhase, uint32_t seed) noexcept
    {
        rng = seed | 1u;
        phase = p.mode == 0 ? freePhase : (double) p.phase;
        held = rand(); prevRand = rand();
        time = 0.0f; releaseTime = -1.0f; finished = false;
        smoothedL = smoothedR = 0.0f; first = true;
    }

    void noteOff() noexcept { if (releaseTime < 0.0f) releaseTime = 0.0f; }

    void tick (const LfoParams& p, float dt, double bpm, const LfoCustomTable* custom) noexcept
    {
        time += dt;
        if (releaseTime >= 0.0f) releaseTime += dt;

        if (! finished && time > p.delay)
        {
            const double prev = phase;
            phase += (double) lfoCyclesPerSecond (p, bpm) * dt;
            if (p.mode == 2 && phase - (double) p.phase >= 1.0) { finished = true; phase = (double) p.phase + 0.999999; }
            if (std::floor (phase) != std::floor (prev)) { prevRand = held; held = rand(); }
        }

        float fade = time < p.delay ? 0.0f : (p.fadeIn > 0.0f ? std::fmin (1.0f, (time - p.delay) / p.fadeIn) : 1.0f);
        if (releaseTime >= 0.0f && p.fadeOut > 0.0f)
            fade *= std::fmax (0.0f, 1.0f - releaseTime / p.fadeOut);

        const float l = shapeValue (p, phase, custom) * fade;
        const float r = shapeValue (p, phase + p.stereo, custom) * fade;
        const float c = p.smooth <= 0.0f ? 0.0f : std::exp (-dt / (p.smooth * p.smooth * 0.5f));
        if (first) { smoothedL = l; smoothedR = r; first = false; }
        smoothedL = l + (smoothedL - l) * c;
        smoothedR = r + (smoothedR - r) * c;
        outL = (smoothedL + p.offset) * p.amount;
        outR = (smoothedR + p.offset) * p.amount;
    }

    float value() const noexcept { return outL; }
    float valueRight() const noexcept { return outR; }

private:
    float shapeValue (const LfoParams& p, double ph, const LfoCustomTable* custom) const noexcept
    {
        const float x = (float) (ph - std::floor (ph));
        float v = 0.0f;
        switch (p.shape)
        {
            case 0: v = std::sin (6.2831853f * x); break;
            case 1: v = x < 0.25f ? 4.0f * x : x < 0.75f ? 2.0f - 4.0f * x : 4.0f * x - 4.0f; break;
            case 2: v = x < 0.5f ? 1.0f : -1.0f; break;
            case 3: v = 2.0f * x - 1.0f; break;
            case 4: v = 1.0f - 2.0f * x; break;
            case 5: v = held; break;
            case 6: { const float s = 0.5f - 0.5f * std::cos (3.14159265f * x); v = prevRand + (held - prevRand) * s; break; }
            case 7:
                if (custom != nullptr)
                {
                    const float pos = x * (float) lfoCustomPoints;
                    const int i0 = (int) pos % lfoCustomPoints, i1 = (i0 + 1) % lfoCustomPoints;
                    const float f = pos - std::floor (pos);
                    v = (*custom)[(size_t) i0].load (std::memory_order_relaxed) * (1.0f - f)
                      + (*custom)[(size_t) i1].load (std::memory_order_relaxed) * f;
                }
                break;
            default: break;
        }
        return p.unipolar ? 0.5f * (v + 1.0f) : v;
    }

    float rand() noexcept
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) (rng >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }

    double phase = 0.0;
    uint32_t rng = 1;
    float held = 0.0f, prevRand = 0.0f, time = 0.0f, releaseTime = -1.0f;
    float smoothedL = 0.0f, smoothedR = 0.0f, outL = 0.0f, outR = 0.0f;
    bool finished = false, first = true;
};
} // namespace chroma
