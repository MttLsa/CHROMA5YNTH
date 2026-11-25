#pragma once
#include "LayerParams.h"
#include "shared/Lfo.h"
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>

namespace chroma
{
// Internal (non-host) parameters of one layer (§1.3): written by GUI/automation slots, read by audio.
class LayerParamStore
{
public:
    LayerParamStore() { init(); }

    void init() noexcept
    {
        for (int i = 0; i < lp::count; ++i)
            values[(size_t) i].store (lp::descs[(size_t) i].def, std::memory_order_relaxed);
        for (auto& t : custom)
            for (int i = 0; i < lfoCustomPoints; ++i)
                t[(size_t) i].store (std::sin (6.2831853f * (float) i / (float) lfoCustomPoints), std::memory_order_relaxed);
    }

    float get (int id) const noexcept { return values[(size_t) id].load (std::memory_order_relaxed); }
    void set (int id, float v) noexcept
    {
        const auto& d = lp::descs[(size_t) id];
        values[(size_t) id].store (juce::jlimit (d.min, d.max, v), std::memory_order_relaxed);
    }

    LfoCustomTable& customTable (int lfo) noexcept { return custom[(size_t) lfo]; }

    juce::ValueTree toTree (const juce::Identifier& type) const
    {
        juce::ValueTree t (type);
        for (int i = 0; i < lp::count; ++i)
            t.setProperty (lp::descs[(size_t) i].id, get (i), nullptr);
        for (size_t l = 0; l < custom.size(); ++l)
        {
            juce::StringArray pts;
            for (auto& p : custom[l]) pts.add (juce::String (p.load(), 5));
            t.setProperty ("lfo" + juce::String ((int) l + 1) + "_custom", pts.joinIntoString (" "), nullptr);
        }
        return t;
    }

    void fromTree (const juce::ValueTree& t)
    {
        init();
        for (int i = 0; i < lp::count; ++i)
            if (t.hasProperty (lp::descs[(size_t) i].id))
                set (i, (float) t.getProperty (lp::descs[(size_t) i].id));
        for (size_t l = 0; l < custom.size(); ++l)
        {
            const auto key = "lfo" + juce::String ((int) l + 1) + "_custom";
            if (! t.hasProperty (key)) continue;
            auto pts = juce::StringArray::fromTokens (t.getProperty (key).toString(), " ", "");
            for (int i = 0; i < juce::jmin (pts.size(), lfoCustomPoints); ++i)
                custom[l][(size_t) i].store (juce::jlimit (-1.0f, 1.0f, pts[i].getFloatValue()));
        }
    }

private:
    std::array<std::atomic<float>, lp::count> values;
    std::array<LfoCustomTable, 4> custom;
};
} // namespace chroma
