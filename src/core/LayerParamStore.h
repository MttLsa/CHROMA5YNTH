#pragma once
#include "LayerParams.h"
#include "shared/Lfo.h"
#include "engines/wavetable/Wavetable.h"
#include "engines/additive/AdditiveSpectrum.h"
#include "engines/pd/PdCurve.h"
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
        touch();
        for (int u = 0; u < wt::numUserSlots; ++u)
            setUserTable (u, {}, 0);
        for (auto& s : addSpectra)
            s.load (add::shapeTable (add::Saw));
        phaseCurve.reset();
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
        v = juce::jlimit (d.min, d.max, v);
        if (std::abs (values[(size_t) id].exchange (v, std::memory_order_relaxed) - v) > 0.0f)
            version.fetch_add (1, std::memory_order_relaxed);
    }
    // Bumped on every edit (undo history); call touch() after editing tables/spectra/curve directly.
    uint32_t getVersion() const noexcept { return version.load (std::memory_order_relaxed); }
    void touch() noexcept { version.fetch_add (1, std::memory_order_relaxed); }

    LfoCustomTable& customTable (int lfo) noexcept { return custom[(size_t) lfo]; }

    // User wavetables (§3D editor): raw frames (wt::frameSize each) kept for state/editing, band-limited
    // table published lock-free to the engine. Message thread only. numFrames = 0 clears the slot.
    struct UserTable { std::vector<float> frames; int numFrames = 0; };
    void setUserTable (int slot, std::vector<float> frames, int numFrames)
    {
        touch();
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

    // Phase Distortion custom phase curve (§3E point editor), read lock-free by the engine.
    // setPoints() from the message thread only.
    pd::PhaseCurve& pdCurve() noexcept { return phaseCurve; }
    const pd::PhaseCurve& pdCurve() const noexcept { return phaseCurve; }

    juce::ValueTree toTree (const juce::Identifier& type) const
    {
        juce::ValueTree t (type);
        t.setProperty ("mmver", 2, nullptr);   // 2: Mod Matrix destinations without the Analog mixer (v1.1)
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
        {
            juce::ValueTree c ("PD_CURVE");
            c.setProperty ("interp", phaseCurve.interpolation(), nullptr);
            juce::StringArray pts;
            for (const auto& q : phaseCurve.points())
                pts.add (juce::String (q.x, 5) + " " + juce::String (q.y, 5));
            c.setProperty ("points", pts.joinIntoString (" "), nullptr);
            t.appendChild (c, nullptr);
        }
        return t;
    }

    void fromTree (const juce::ValueTree& t)
    {
        init();
        for (int i = 0; i < lp::count; ++i)
            if (t.hasProperty (lp::descs[(size_t) i].id))
                set (i, (float) t.getProperty (lp::descs[(size_t) i].id));
        if ((int) t.getProperty ("mmver", 1) < 2)
            for (auto id : lp::mmDest)
                set (id, (float) upgradeDestV1 ((int) std::lround (get (id))));
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
        {
            const auto c = t.getChildWithName ("PD_CURVE");
            if (c.isValid())
            {
                auto v = juce::StringArray::fromTokens (c.getProperty ("points").toString(), " ", "");
                std::vector<pd::PhaseCurve::Point> pts;
                for (int i = 0; i + 1 < v.size(); i += 2)
                    pts.push_back ({ v[i].getFloatValue(), v[i + 1].getFloatValue() });
                phaseCurve.setPoints (std::move (pts), (int) c.getProperty ("interp", 0));
            }
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

    // v1.0 state: Mod Matrix destination index -> current index (the Analog mixer was removed in v1.1).
    static int upgradeDestV1 (int i)
    {
        static const char* const oldAnalog[] { "OSC 1 Fine", "OSC 1 Level", "OSC 1 Pan", "OSC 1 Phase", "OSC 1 Pulse Width",
            "OSC 1 SuperSaw Detune", "OSC 1 SuperSaw Spread", "OSC 1 Stereo Width", "OSC 1 Wave Mod", "OSC 2 Fine", "OSC 2 Level",
            "OSC 2 Pan", "OSC 2 Phase", "OSC 2 Pulse Width", "OSC 2 SuperSaw Detune", "OSC 2 SuperSaw Spread", "OSC 2 Stereo Width",
            "OSC 2 Wave Mod", "OSC 3 Fine", "OSC 3 Level", "OSC 3 Pan", "OSC 3 Phase", "OSC 3 Pulse Width", "OSC 3 SuperSaw Detune",
            "OSC 3 SuperSaw Spread", "OSC 3 Stereo Width", "OSC 3 Wave Mod", "Mixer OSC 1 Volume", "Mixer OSC 1 Pan",
            "Mixer OSC 2 Volume", "Mixer OSC 2 Pan", "Mixer OSC 3 Volume", "Mixer OSC 3 Pan", "FM 3\xe2\x86\x92" "1", "FM 2\xe2\x86\x92" "1",
            "Ring Mod 1\xc3\x97" "2", "Noise Level", "Sub Level", "Output Level", "Sub Phase", "Noise Color", "Noise Pan", "Noise Stereo Width" };
        constexpr int oldCount = (int) (sizeof (oldAnalog) / sizeof (oldAnalog[0]));
        const int n = lp::list_modDestinations_size;
        int a = 0;
        while (a < n && std::strcmp (lp::list_modDestinations[a], "OSC 1 Fine") != 0) ++a;
        if (a >= n || i < a) return i;
        const int newCount = (int) [&] { int k = a; while (k < n && std::strncmp (lp::list_modDestinations[k], "Op 1", 4) != 0) ++k; return k - a; }();
        if (i >= a + oldCount) return i - oldCount + newCount;
        for (int k = a; k < a + newCount; ++k)
            if (std::strcmp (lp::list_modDestinations[k], oldAnalog[i - a]) == 0) return k;
        return 0;   // mixer destinations no longer exist
    }

private:
    std::array<std::atomic<float>, lp::count> values;
    std::atomic<uint32_t> version { 0 };
    std::array<LfoCustomTable, 4> custom;
    std::array<UserTable, wt::numUserSlots> userRaw;
    std::array<wt::TableSlot, wt::numUserSlots> userSlots;
    std::array<add::CustomSpectrum, 2> addSpectra;
    pd::PhaseCurve phaseCurve;
};
} // namespace chroma
