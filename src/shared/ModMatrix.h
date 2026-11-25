#pragma once
#include "LayerParams.h"
#include <array>
#include <cmath>

namespace chroma
{
// MOD MATRIX (§2): 16 slots per layer. Order must match `mod_sources` in params/layer_engine.yaml.
enum class ModSource : int
{
    Off, Lfo1, Lfo2, Lfo3, Lfo4, AmpEnv, ModEnv1, ModEnv2, ModEnv3,
    Velocity, ReleaseVelocity, Aftertouch, MpePressure, MpeSlide, MpePitch,
    ModWheel, PitchWheel, KeyTrack, NoteNumber, Random, Breath, Expression, MidiCC,
    Macro1, Macro2, Macro3, Macro4, Macro5, Macro6, Macro7, Macro8,
    Count
};
static_assert ((int) ModSource::Count == lp::list_modSources_size, "ModSource out of sync with params yaml");

inline constexpr int modSlots = 16;

struct ModSlot
{
    bool enabled = false;
    int source = 0, via = 0, cc = 1, curve = 0;
    int dest = -1;            // lp::Id, -1 = off
    float amount = 0.0f, smooth = 0.0f;
    bool invert = false;
};

struct ModSourceValues
{
    std::array<float, (size_t) ModSource::Count> v {};
    const std::array<float, 128>* cc = nullptr;   // controllers of the voice's channel

    float get (int src, int ccNumber) const noexcept
    {
        if (src == (int) ModSource::MidiCC)
            return cc != nullptr ? (*cc)[(size_t) (ccNumber & 127)] : 0.0f;
        return src > 0 && src < (int) ModSource::Count ? v[(size_t) src] : 0.0f;
    }
};

inline float applyModCurve (float x, int curve) noexcept
{
    const float a = std::fabs (x), s = x < 0.0f ? -1.0f : 1.0f;
    switch (curve)
    {
        case 1: return s * a * a;
        case 2: return s * std::sqrt (a);
        case 3: { float u = 0.5f * (x + 1.0f); u = u * u * (3.0f - 2.0f * u); return 2.0f * u - 1.0f; }
        default: return x;
    }
}

// Per-voice matrix state: smoothing memory and the destinations written last tick.
class ModMatrixState
{
public:
    void reset() noexcept { smoothed.fill (0.0f); first = true; }

    // Accumulates normalised offsets into `offsets` (indexed by lp::Id). Clears stale entries first.
    void process (const std::array<ModSlot, modSlots>& slots, const ModSourceValues& src, float dt,
                  float* offsets) noexcept
    {
        for (int i = 0; i < numTouched; ++i)
            offsets[touched[(size_t) i]] = 0.0f;
        numTouched = 0;

        for (size_t i = 0; i < (size_t) modSlots; ++i)
        {
            const auto& s = slots[i];
            if (! s.enabled || s.dest < 0 || s.source == 0)
                continue;
            float x = applyModCurve (src.get (s.source, s.cc), s.curve);
            if (s.invert) x = -x;
            float m = x * s.amount;
            if (s.via != 0) m *= src.get (s.via, s.cc);

            if (s.smooth > 0.0f && ! first)
                m = m + (smoothed[i] - m) * std::exp (-dt / (s.smooth * 0.5f));
            smoothed[i] = m;

            offsets[s.dest] += m;
            touched[(size_t) numTouched++] = s.dest;
        }
        first = false;
    }

private:
    std::array<float, modSlots> smoothed {};
    std::array<int, modSlots> touched {};
    int numTouched = 0;
    bool first = true;
};
} // namespace chroma
