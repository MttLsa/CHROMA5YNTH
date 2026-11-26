#pragma once
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>

namespace chroma::add
{
inline constexpr int maxPartials = 256;

// Per-partial fields of a spectrum (§3B editor: bars + numeric).
enum Field { fRatio, fFreq, fAmp, fPhase, fDetune, fFine, fPan, fPolarity, fMute, fSolo, numFields };

struct FieldInfo { const char* name; float min, max, def; };
const FieldInfo& fieldInfo (int field) noexcept;   // Ratio, Freq (Hz, > 0 = fixed), Amp, Phase (cycles), …

// Structure-of-arrays spectrum: v[field][partial] (partial 0 = fundamental).
struct SpectrumData
{
    std::array<std::array<float, maxPartials>, numFields> v {};
    float& at (int field, int k) noexcept { return v[(size_t) field][(size_t) k]; }
    float at (int field, int k) const noexcept { return v[(size_t) field][(size_t) k]; }
};

// Factory shapes, indices of the `add_shapes` list (0 = Custom → the layer's drawn table).
enum Shape { Custom, Sine, Saw, Square, Triangle, Pulse25, Flat, Organ, Hollow, Bell, VocalA, VocalO, Strings, Glass, numShapes };
void makeShape (int shape, SpectrumData& out) noexcept;
const SpectrumData& shapeTable (int shape) noexcept;   // cached; first call builds all (call off the audio thread)

// Custom spectrum of a layer: written by the editor (message thread), read lock-free by the audio thread.
class CustomSpectrum
{
public:
    CustomSpectrum() { load (shapeTable (Saw)); }
    float get (int field, int k) const noexcept { return v[(size_t) field][(size_t) k].load (std::memory_order_relaxed); }
    uint32_t version() const noexcept { return ver.load (std::memory_order_acquire); }   // changes on every edit
    void set (int field, int k, float x) noexcept;   // clamped to the field range
    void load (const SpectrumData& d) noexcept
    {
        for (int f = 0; f < numFields; ++f)
            for (int k = 0; k < maxPartials; ++k)
                set (f, k, d.at (f, k));
    }
    void copyTo (SpectrumData& d, int n) const noexcept
    {
        for (int f = 0; f < numFields; ++f)
            for (int k = 0; k < n; ++k)
                d.at (f, k) = get (f, k);
    }

private:
    std::array<std::array<std::atomic<float>, maxPartials>, numFields> v;
    std::atomic<uint32_t> ver { 1 };
};

// Fast approximations (vectorisable, ~1e-4 relative error) used at control rate.
inline float fastLog2 (float x) noexcept   // x > 0
{
    const auto b = std::bit_cast<uint32_t> (x);
    const float e = (float) (int) ((b >> 23) & 255u) - 127.0f;
    const float m = std::bit_cast<float> ((b & 0x007fffffu) | 0x3f800000u);   // [1, 2)
    return e + (-2.78681295f + m * (5.04687604f + m * (-3.49249428f + m * (1.59390136f + m * (-0.40486717f + m * 0.04342891f)))));
}

inline float fastExp2 (float x) noexcept
{
    x = x < -126.0f ? -126.0f : x > 126.0f ? 126.0f : x;
    const float t = (float) (int) x;
    const float fl = x < t ? t - 1.0f : t;
    const float f = x - fl;
    const float p = 0.99999977f + f * (0.69315678f + f * (0.24013169f + f * (0.05587656f + f * (0.00894058f + f * 0.00189438f))));
    return std::bit_cast<float> ((uint32_t) ((int) fl + 127) << 23) * p;
}

// sin(2π·x), |x| < 2^22: round-to-nearest via the 1.5·2^23 trick, then x·(¼ − x²)·P(x²) on [−½, ½]
// (zeros built in, no branches → vectorises along samples). Max error ≈ 2.5e-7 + phase rounding.
inline float sin2piReduced (float x) noexcept
{
    constexpr float magic = 12582912.0f;
    x -= (x + magic) - magic;
    const float u = x * x;
    return x * (0.25f - u) * (25.13273694f + u * (-64.83489672f + u * (67.04452264f + u * (-38.110589f + u * 12.17799095f))));
}

inline float sin2pi (float x) noexcept { return sin2piReduced (x); }
} // namespace chroma::add
