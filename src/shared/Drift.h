#pragma once
#include "engines/IEngine.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace chroma
{
// DRIFT (§2): slow random pitch micro-variations per voice / oscillator plus filter component tolerance.
// Control rate only; values are deterministic per seed.
class Drift
{
public:
    static constexpr float maxCents = 12.0f;        // at amount 1
    static constexpr float maxCutoffOct = 0.25f;    // tolerance at amount 1
    static constexpr float maxResOffset = 0.04f;

    void start (uint32_t seed) noexcept
    {
        rng = seed | 1u;
        for (size_t i = 0; i < walk.size(); ++i)
        {
            walk[i].cur = walk[i].target = bipolar();
            walk[i].timer = interval();
        }
        cutTol = bipolar();
        resTol = bipolar();
    }

    // Advances the random walks by dt seconds and refreshes the outputs.
    void tick (float amount, float dt) noexcept
    {
        const float c = 1.0f - std::exp (-dt / 0.6f);
        for (auto& w : walk)
        {
            w.timer -= dt;
            if (w.timer <= 0.0f) { w.target = bipolar(); w.timer = interval(); }
            w.cur += (w.target - w.cur) * c;
        }
        const float common = walk[maxUnison].cur;
        for (int i = 0; i < maxUnison; ++i)
            semis[(size_t) i] = amount * maxCents * 0.01f * (0.6f * walk[(size_t) i].cur + 0.4f * common);
        cutoffOct = amount * maxCutoffOct * (0.8f * cutTol + 0.2f * common);
        resOffset = amount * maxResOffset * resTol;
    }

    const float* pitchSemis() const noexcept { return semis.data(); }
    float cutoffOctaves() const noexcept { return cutoffOct; }
    float resonanceOffset() const noexcept { return resOffset; }

private:
    struct Walk { float cur = 0.0f, target = 0.0f, timer = 0.0f; };
    float uni() noexcept { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) * (1.0f / 16777216.0f); }
    float bipolar() noexcept { return uni() * 2.0f - 1.0f; }
    float interval() noexcept { return 0.3f + 1.2f * uni(); }

    uint32_t rng = 1;
    std::array<Walk, maxUnison + 1> walk {};   // last = common (per voice)
    std::array<float, maxUnison> semis {};
    float cutTol = 0.0f, resTol = 0.0f, cutoffOct = 0.0f, resOffset = 0.0f;
};
} // namespace chroma
