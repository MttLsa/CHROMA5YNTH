#pragma once
#include <cmath>

namespace chroma
{
// One-pole smoother advanced in steps of `n` samples (control rate).
struct OnePole
{
    float value = 0.0f;
    float coeffPerSample = 0.0f; // exp(-1/(tau*sr))

    void setTime (float seconds, double sampleRate) noexcept
    {
        coeffPerSample = seconds <= 0.0f ? 0.0f : (float) std::exp (-1.0 / (seconds * sampleRate));
    }
    void reset (float v) noexcept { value = v; }
    float step (float target, int n) noexcept
    {
        const float c = n == 1 ? coeffPerSample : std::pow (coeffPerSample, (float) n);
        value = target + (value - target) * c;
        return value;
    }
};

// Linear ramp across a block, used to de-zipper gains.
struct LinearRamp
{
    float current = 0.0f, target = 0.0f, inc = 0.0f;
    void reset (float v) noexcept { current = target = v; inc = 0.0f; }
    void setTarget (float t, int samples) noexcept
    {
        target = t;
        inc = samples > 0 ? (t - current) / (float) samples : 0.0f;
        if (samples <= 0) current = t;
    }
    float next() noexcept
    {
        current += inc;
        if ((inc > 0.0f && current > target) || (inc < 0.0f && current < target)) { current = target; inc = 0.0f; }
        return current;
    }
};
} // namespace chroma
