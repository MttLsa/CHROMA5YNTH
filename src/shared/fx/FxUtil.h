#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// Building blocks shared by every FX (Layer FX §2 and Master FX §4). Audio thread: no allocation
// after prepare(), no locks.
namespace chroma::fx
{
inline constexpr float pi = 3.14159265358979f;
inline constexpr float twoPi = 6.28318530717959f;

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float flushDenormal (float x) noexcept { return std::fabs (x) < 1.0e-15f ? 0.0f : x; }

// Per-sample one-pole smoother (de-zipper of control-rate parameters).
struct Smoothed
{
    float value = 0.0f, target = 0.0f, coeff = 0.0f;
    void setTime (float seconds, double sr) noexcept { coeff = seconds <= 0.0f ? 0.0f : (float) std::exp (-1.0 / (seconds * sr)); }
    void reset (float v) noexcept { value = target = v; }
    float next() noexcept { value = target + (value - target) * coeff; return value; }
};

// Power-of-two circular delay line with cubic (Hermite) fractional read.
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        int size = 4;
        while (size < maxDelaySamples + 4) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1;
        w = 0;
    }
    void clear() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    int capacity() const noexcept { return mask - 3; }

    void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }

    // d = 0 is the most recently pushed sample.
    float tap (int d) const noexcept { return buf[(size_t) ((w - 1 - d) & mask)]; }

    float read (float d) const noexcept
    {
        d = std::clamp (d, 1.0f, (float) capacity());
        const int i = (int) d;
        const float f = d - (float) i;
        const float xm1 = tap (i - 1), x0 = tap (i), x1 = tap (i + 1), x2 = tap (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    float readLinear (float d) const noexcept
    {
        d = std::clamp (d, 0.0f, (float) capacity());
        const int i = (int) d;
        const float f = d - (float) i;
        return tap (i) + f * (tap (i + 1) - tap (i));
    }

private:
    std::vector<float> buf;
    int mask = 0, w = 0;
};

// TPT state-variable filter (Zavalishin / Simper), modulation-stable.
struct Svf
{
    float g = 0.0f, k = 1.414f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
    float lp = 0.0f, bp = 0.0f, hp = 0.0f;

    void reset() noexcept { ic1 = ic2 = 0.0f; }
    void set (float freq, float q, float sr) noexcept
    {
        freq = std::clamp (freq, 5.0f, 0.49f * sr);
        g = std::tan (pi * freq / sr);
        k = 1.0f / std::max (q, 0.025f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void process (float x) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flushDenormal (2.0f * v1 - ic1);
        ic2 = flushDenormal (2.0f * v2 - ic2);
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
};

// TPT one-pole (LP + HP outputs).
struct OnePole
{
    float g = 0.0f, s = 0.0f;
    void reset() noexcept { s = 0.0f; }
    void set (float freq, float sr) noexcept
    {
        const float t = std::tan (pi * std::clamp (freq, 1.0f, 0.49f * sr) / sr);
        g = t / (1.0f + t);
    }
    float lp (float x) noexcept { const float v = (x - s) * g; const float y = v + s; s = flushDenormal (y + v); return y; }
    float hp (float x) noexcept { return x - lp (x); }
};

// Biquad (RBJ cookbook) in transposed direct form II, for shelves/peaks.
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;
    void reset() noexcept { z1 = z2 = 0.0f; }
    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = flushDenormal (b1 * x - a1 * y + z2);
        z2 = flushDenormal (b2 * x - a2 * y);
        return y;
    }
    enum class Kind { LowShelf, Peak, HighShelf };
    void design (Kind kind, float freq, float q, float gainDb, float sr) noexcept
    {
        const float A = std::pow (10.0f, gainDb / 40.0f);
        const float w0 = twoPi * std::clamp (freq, 10.0f, 0.49f * sr) / sr;
        const float cw = std::cos (w0), sw = std::sin (w0);
        const float alpha = sw / (2.0f * std::max (q, 0.05f));
        float B0, B1, B2, A0, A1, A2;
        if (kind == Kind::Peak)
        {
            B0 = 1.0f + alpha * A; B1 = -2.0f * cw; B2 = 1.0f - alpha * A;
            A0 = 1.0f + alpha / A; A1 = -2.0f * cw; A2 = 1.0f - alpha / A;
        }
        else
        {
            const float sA = 2.0f * std::sqrt (A) * alpha;
            const float s = kind == Kind::LowShelf ? 1.0f : -1.0f;
            B0 = A * ((A + 1.0f) - s * (A - 1.0f) * cw + sA);
            B1 = s * 2.0f * A * ((A - 1.0f) - s * (A + 1.0f) * cw);
            B2 = A * ((A + 1.0f) - s * (A - 1.0f) * cw - sA);
            A0 = (A + 1.0f) + s * (A - 1.0f) * cw + sA;
            A1 = -s * 2.0f * ((A - 1.0f) + s * (A + 1.0f) * cw);
            A2 = (A + 1.0f) + s * (A - 1.0f) * cw - sA;
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
};

// Cheap sine/triangle LFO with explicit phase (0..1).
struct FxLfo
{
    double phase = 0.0;
    void reset (double p = 0.0) noexcept { phase = p; }
    void advance (float hz, float sr) noexcept { phase += hz / sr; phase -= std::floor (phase); }
    static float sine (double p) noexcept { return std::sin (twoPi * (float) (p - std::floor (p))); }
    static float tri (double p) noexcept { const float x = (float) (p - std::floor (p)); return 4.0f * std::fabs (x - 0.5f) - 1.0f; }
};

struct Rng
{
    uint32_t s = 0x9E3779B9u;
    float next() noexcept { s = s * 1664525u + 1013904223u; return (float) (int32_t) s * (1.0f / 2147483648.0f); }
};

// Seconds of one sync division (same list as `sync_divisions`: 1/1..1/32, T, D).
inline float divisionSeconds (int index, double bpm) noexcept
{
    static constexpr float beats[] { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f };
    const int i = std::clamp (index, 0, 17);
    const float b = beats[i % 6] * (i >= 12 ? 1.5f : i >= 6 ? 2.0f / 3.0f : 1.0f);
    return b * 60.0f / (float) std::max (bpm, 1.0);
}

// Click-free on/off: fades a processed path in/out against the dry signal.
struct Bypass
{
    float g = 0.0f, step = 0.0f;
    bool target = false;
    void prepare (double sr) noexcept { step = (float) (1.0 / (0.005 * sr)); }
    void reset (bool on) noexcept { target = on; g = on ? 1.0f : 0.0f; }
    void set (bool on) noexcept { target = on; }
    bool idle() const noexcept { return ! target && g <= 0.0f; }
    bool justStarting() const noexcept { return target && g <= 0.0f; }
    bool steady() const noexcept { return target && g >= 1.0f; }
    float next() noexcept { g = std::clamp (g + (target ? step : -step), 0.0f, 1.0f); return g; }
};
} // namespace chroma::fx
