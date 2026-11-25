#pragma once
#include "LayerParams.h"
#include <algorithm>
#include <cmath>

namespace chroma
{
// Normalised <-> plain conversion for internal layer parameters (same skew law as JUCE NormalisableRange).
struct ParamRange
{
    float min = 0.0f, max = 1.0f, skew = 1.0f;
    bool discrete = false;

    float toNorm (float v) const noexcept
    {
        const float p = std::clamp ((v - min) / (max - min), 0.0f, 1.0f);
        return skew == 1.0f ? p : std::pow (p, skew);
    }

    float fromNorm (float n) const noexcept
    {
        n = std::clamp (n, 0.0f, 1.0f);
        if (skew != 1.0f && n > 0.0f)
            n = std::exp (std::log (n) / skew);
        const float v = min + (max - min) * n;
        return discrete ? std::round (v) : v;
    }
};

inline const ParamRange& paramRange (int id) noexcept
{
    static const auto table = []
    {
        std::array<ParamRange, lp::count> t {};
        for (int i = 0; i < lp::count; ++i)
        {
            const auto& d = lp::descs[(size_t) i];
            auto& r = t[(size_t) i];
            r.min = d.min;
            r.max = d.max;
            r.discrete = d.type != lp::Type::Float;
            if (d.hasSkew && d.skewCentre > d.min && d.skewCentre < d.max)
                r.skew = std::log (0.5f) / std::log ((d.skewCentre - d.min) / (d.max - d.min));
        }
        return t;
    }();
    return table[(size_t) id];
}
} // namespace chroma
