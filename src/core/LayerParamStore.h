#pragma once
#include "LayerParams.h"
#include "shared/Lfo.h"
#include "engines/wavetable/Wavetable.h"
#include "engines/additive/AdditiveSpectrum.h"
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace chroma
{
// Internal (non-host) parameters of one layer (§1.3): written by GUI/automation slots, read by audio.
class LayerParamStore
{
public:
    LayerParamStore() { init(); }

    void init()
    {
        for (int u = 0; u < wt::numUserSlots; ++u)
            setUserTable (u, {}, 0);
        for (auto& s : addSpectra)
            s.load (add::shapeTable (add::Saw));
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

    // User wavetables (§3D editor): raw frames (wt::frameSize each) kept for state/editing, band-limited
    // table published lock-free to the engine. Message thread only. numFrames = 0 clears the slot.
    struct UserTable { std::vector<float> frames; int numFrames = 0; };
    void setUserTable (int slot, std::vector<float> frames, int numFrames)
    {
        auto& u = userRaw[(size_t) slot];
        numFrames = juce::jlimit (0, wt::maxFrames, juce::jmin (numFrames, (int) (frames.size() / (size_t) wt::frameSize)));
        frames.resize ((size_t) numFrames * wt::frameSize);
        u.frames = std::move (frames);
        u.numFrames = numFrames;
        userSlots[(size_t) slot].set (numFrames > 0 ? wt::buildTable (u.frames.data(), numFrames,
                                                                      "User " + std::to_string (slot + 1), true)
                                                    : nullptr);
    }
    const UserTable& userTable (int slot) const noexcept { return userRaw[(size_t) slot]; }
    const wt::TableSlot* userTableSlots() const noexcept { return userSlots.data(); }

    // Additive Custom spectra A (0) / B (1) (§3B bar editor), read lock-free by the engine.
    add::CustomSpectrum& additiveSpectrum (int ab) noexcept { return addSpectra[(size_t) (ab != 0)]; }
    const add::CustomSpectrum& additiveSpectrum (int ab) const noexcept { return addSpectra[(size_t) (ab != 0)]; }

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
        for (int u = 0; u < wt::numUserSlots; ++u)
        {
            const auto& ut = userRaw[(size_t) u];
            if (ut.numFrames <= 0) continue;
            juce::ValueTree w ("WT_USER");
            w.setProperty ("slot", u, nullptr);
            w.setProperty ("frames", ut.numFrames, nullptr);
            juce::MemoryBlock mb (ut.frames.data(), ut.frames.size() * sizeof (float));
            w.setProperty ("data", mb.toBase64Encoding(), nullptr);
            t.appendChild (w, nullptr);
        }
        for (size_t s = 0; s < addSpectra.size(); ++s)
        {
            std::vector<float> d ((size_t) add::numFields * add::maxPartials);
            for (int f = 0; f < add::numFields; ++f)
                for (int k = 0; k < add::maxPartials; ++k)
                    d[(size_t) (f * add::maxPartials + k)] = addSpectra[s].get (f, k);
            juce::ValueTree a ("ADD_SPECTRUM");
            a.setProperty ("slot", (int) s, nullptr);
            a.setProperty ("fields", add::numFields, nullptr);
            juce::MemoryBlock mb (d.data(), d.size() * sizeof (float));
            a.setProperty ("data", mb.toBase64Encoding(), nullptr);
            t.appendChild (a, nullptr);
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
        for (auto a : t)
        {
            if (! a.hasType ("ADD_SPECTRUM")) continue;
            const int s = a.getProperty ("slot", -1), nf = a.getProperty ("fields", 0);
            juce::MemoryBlock mb;
            if (s < 0 || s > 1 || nf <= 0 || ! mb.fromBase64Encoding (a.getProperty ("data").toString())
                || mb.getSize() < (size_t) nf * add::maxPartials * sizeof (float))
                continue;
            const auto* d = static_cast<const float*> (mb.getData());
            for (int f = 0; f < juce::jmin (nf, (int) add::numFields); ++f)
                for (int k = 0; k < add::maxPartials; ++k)
                    addSpectra[(size_t) s].set (f, k, d[(size_t) (f * add::maxPartials + k)]);
        }
        for (auto w : t)
        {
            if (! w.hasType ("WT_USER")) continue;
            const int u = w.getProperty ("slot", -1), nf = w.getProperty ("frames", 0);
            juce::MemoryBlock mb;
            if (u < 0 || u >= wt::numUserSlots || nf <= 0 || ! mb.fromBase64Encoding (w.getProperty ("data").toString()))
                continue;
            std::vector<float> fr (mb.getSize() / sizeof (float));
            std::memcpy (fr.data(), mb.getData(), fr.size() * sizeof (float));
            for (auto& x : fr) if (! std::isfinite (x)) x = 0.0f;
            setUserTable (u, std::move (fr), nf);
        }
    }

private:
    std::array<std::atomic<float>, lp::count> values;
    std::array<LfoCustomTable, 4> custom;
    std::array<UserTable, wt::numUserSlots> userRaw;
    std::array<wt::TableSlot, wt::numUserSlots> userSlots;
    std::array<add::CustomSpectrum, 2> addSpectra;
};
} // namespace chroma
