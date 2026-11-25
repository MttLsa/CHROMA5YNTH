#pragma once
#include <array>

namespace chroma
{
// Band-limited step / ramp corrections from the integrated cubic B-spline (4-point BLEP / BLAMP).
// An event (value jump h or slope jump d, per sample) happening x samples before sample n (0 <= x < 1)
// is corrected by adding h * blep[k] (or d * blamp[k]) to samples n-2, n-1, n, n+1 (k = 0..3).
// Output therefore needs 2 samples of look-ahead: callers write naive sample i at buffer index i + 2.
namespace blep
{
// Integral of the cubic B-spline (support [-2, 2]) at s, minus the unit step.
inline float stepResidual (float s) noexcept
{
    const auto half = [] (float u) noexcept   // integral for u <= 0
    {
        if (u <= -2.0f) return 0.0f;
        if (u <= -1.0f) { const float a = u + 2.0f; return a * a * a * a * (1.0f / 24.0f); }
        const float u2 = u * u;
        return 1.0f / 24.0f + (4.0f * u - 2.0f * u2 * u - 0.75f * u2 * u2 + 2.75f) * (1.0f / 6.0f);
    };
    return s < 0.0f ? half (s) : -half (-s);
}

// Second integral of the cubic B-spline minus the unit ramp max(s, 0) (even function).
inline float rampResidual (float s) noexcept
{
    const float u = s > 0.0f ? -s : s;   // R(s) = D(-|s|)
    if (u <= -2.0f) return 0.0f;
    if (u <= -1.0f) { const float a = u + 2.0f; return a * a * a * a * a * (1.0f / 120.0f); }
    const auto F = [] (float v) noexcept
    {
        const float v2 = v * v;
        return v * (1.0f / 24.0f) + (2.0f * v2 - 0.5f * v2 * v2 - 0.15f * v2 * v2 * v + 2.75f * v) * (1.0f / 6.0f);
    };
    return 1.0f / 120.0f + F (u) - F (-1.0f);
}

// Adds a step of height h at fractional time x (0..1 before index pos) to buf[pos-2 .. pos+1].
inline void addStep (float* buf, int pos, float x, float h) noexcept
{
    buf[pos - 2] += h * stepResidual (x - 2.0f);
    buf[pos - 1] += h * stepResidual (x - 1.0f);
    buf[pos]     += h * stepResidual (x);
    buf[pos + 1] += h * stepResidual (x + 1.0f);
}

inline void addRamp (float* buf, int pos, float x, float d) noexcept
{
    buf[pos - 2] += d * rampResidual (x - 2.0f);
    buf[pos - 1] += d * rampResidual (x - 1.0f);
    buf[pos]     += d * rampResidual (x);
    buf[pos + 1] += d * rampResidual (x + 1.0f);
}
} // namespace blep
} // namespace chroma
