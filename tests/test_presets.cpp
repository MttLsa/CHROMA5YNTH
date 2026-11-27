// §6 factory presets + tools/presetnorm loudness normalisation.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "TestHelpers.h"
#include "preset/Loudness.h"
#include "preset/PresetNormalizer.h"
#include <cmath>
#include <set>

using Catch::Approx;
using namespace chroma;

namespace
{
std::vector<std::vector<float>> stereoSine (double hz, double amp, double fs, double sec, double phase = 0.0)
{
    std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) (fs * sec)));
    for (size_t i = 0; i < x[0].size(); ++i)
        x[0][i] = x[1][i] = (float) (amp * std::sin (2.0 * 3.14159265358979 * hz * (double) i / fs + phase));
    return x;
}

struct TempDir
{
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("c5factory", "", false);
    TempDir() { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }
};
} // namespace

TEST_CASE ("Presets: BS.1770 short-term loudness and true peak", "[presets]")
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        // 997 Hz sine at 0 dBFS in both channels = 0 LUFS (−3.01 per channel).
        REQUIRE (loudness::shortTermMax (stereoSine (997.0, 1.0, fs, 4.0), fs) == Approx (0.0).margin (0.1));
        REQUIRE (loudness::shortTermMax (stereoSine (997.0, 0.1, fs, 4.0), fs) == Approx (-20.0).margin (0.1));
        // K-weighting: +~4 dB shelf at high frequencies, low cut.
        REQUIRE (loudness::shortTermMax (stereoSine (10000.0, 0.1, fs, 4.0), fs) > -17.0);
        REQUIRE (loudness::shortTermMax (stereoSine (20.0, 0.1, fs, 4.0), fs) < -32.0);
    }
    // Short-term max: a 1 s burst inside 4 s of silence reads 10·log10(1/3) below its level.
    auto burst = stereoSine (997.0, 1.0, 48000.0, 4.0);
    for (size_t i = 0; i < burst[0].size(); ++i)
        if (i < 48000 || i >= 96000) burst[0][i] = burst[1][i] = 0.0f;
    REQUIRE (loudness::shortTermMax (burst, 48000.0) == Approx (-4.77).margin (0.15));

    // Inter-sample peak: fs/4 sine at 45° has samples at 0.707 but a true peak of 1.0.
    const auto isp = stereoSine (12000.0, 1.0, 48000.0, 0.5, 3.14159265358979 / 4.0);
    REQUIRE (loudness::truePeakDb (isp) == Approx (0.0).margin (0.25));
    REQUIRE (loudness::truePeakDb (stereoSine (100.0, 0.5, 48000.0, 0.5)) == Approx (-6.02).margin (0.05));
}

TEST_CASE ("Presets: trim parameters, normalisation and compact files", "[presets]")
{
    // preset_trim adds to the master level, layer_trim to the layer level.
    test::Rig rig;
    juce::ValueTree multi = rig.proc.captureState (false);
    const auto m0 = PresetNormalizer::measure (PresetKind::Multi, multi);
    REQUIRE (m0.lufs > -40.0);
    PresetNormalizer::setTrim (PresetKind::Multi, multi, -6.0f);
    const auto m1 = PresetNormalizer::measure (PresetKind::Multi, multi);
    REQUIRE (m1.lufs - m0.lufs == Approx (-6.0).margin (0.1));

    auto layer = rig.synth().copyLayer (0);
    PresetNormalizer::setTrim (PresetKind::Layer, layer, -10.0f);
    const auto l1 = PresetNormalizer::measure (PresetKind::Layer, layer);
    REQUIRE (l1.lufs - m0.lufs == Approx (-10.0).margin (0.1));

    PresetNormalizer::Result r;
    const auto norm = PresetNormalizer::normalize (PresetKind::Multi, multi, &r);
    REQUIRE_FALSE (r.silent);
    REQUIRE (r.after.truePeak <= PresetNormalizer::ceilingDbtp + 0.01);
    if (! r.peakLimited) REQUIRE (r.after.lufs == Approx (PresetNormalizer::targetLufs).margin (0.1));

    // Compact files keep only non-default values and load back to the same state.
    const auto compact = PresetNormalizer::compact (PresetKind::Multi, norm);
    REQUIRE (compact.getNumChildren() < 10);
    test::Rig a, b;
    a.proc.restoreState (norm, true);
    b.proc.restoreState (compact, true);
    REQUIRE (a.proc.captureState (false).isEquivalentTo (b.proc.captureState (false)));
    REQUIRE (PresetNormalizer::getTrim (PresetKind::Multi, compact) == Approx (r.trimDb));
}

TEST_CASE ("Presets: compaction keeps the Mod Matrix destinations", "[presets]")
{
    ChromaProcessor proc;
    auto& st = proc.getSynth().layerStore (0);
    st.set (lp::mmSource[0], 1.0f);
    st.set (lp::mmDest[0], (float) (lp::list_modDestinations_size - 3));   // near the end of the list
    for (auto kind : { PresetKind::Layer, PresetKind::Multi })
    {
        const auto full = kind == PresetKind::Layer ? proc.getSynth().copyLayer (0) : proc.captureState (false);
        const auto c = PresetNormalizer::compact (kind, full);
        ChromaProcessor other;
        if (kind == PresetKind::Layer) other.getSynth().pasteLayer (0, c);
        else other.restoreState (c, false);
        CHECK ((int) other.getSynth().layerStore (0).get (lp::mmDest[0]) == lp::list_modDestinations_size - 3);
    }
}

TEST_CASE ("Presets: factory set", "[presets]")
{
    const int n = PresetManager::numFactoryPresets();
    REQUIRE (n > 0);
    TempDir tmp;
    PresetManager pm (tmp.dir);
    REQUIRE (pm.installFactory() == n);
    REQUIRE (pm.installFactory() == 0);              // marker: once per factory set
    REQUIRE (pm.installFactory (true) == n);
    {   // presets of an older factory set disappear when the set changes; user presets stay
        const auto stale = tmp.dir.getChildFile ("Layer/Factory/Old Factory Patch.c5layer");
        const auto mine = tmp.dir.getChildFile ("Layer/My Patch.c5layer");
        REQUIRE (stale.replaceWithText ("x"));
        REQUIRE (mine.replaceWithText ("x"));
        tmp.dir.getChildFile (".factory").replaceWithText ("old");
        REQUIRE (pm.installFactory() == n);
        REQUIRE_FALSE (stale.existsAsFile());
        REQUIRE (mine.existsAsFile());
        mine.deleteFile();
        pm.rescan();
    }

    int multis = 0, layers = 0;
    std::set<juce::String> names;
    std::array<int, 5> perEngine {};
    juce::String firstMulti;
    for (const auto& info : pm.all())
    {
        INFO (info.meta.name.toStdString());
        REQUIRE (info.file.getParentDirectory().getFileName() == "Factory");
        REQUIRE (info.meta.category.isNotEmpty());
        REQUIRE (PresetManager::defaultCategories().contains (info.meta.category));
        REQUIRE (names.insert (info.meta.name).second);
        auto content = PresetManager::load (info.file);
        REQUIRE (content);
        test::Rig rig;
        if (info.kind == PresetKind::Layer)
        {
            // LAYER: one synth, playing every note
            ++layers;
            REQUIRE (content->hasType ("LAYER_PRESET"));
            rig.proc.loadLayerPreset (2, info.file);
            continue;
        }
        // MULTI: all 12 synths on, NOTE ASSIGN on, synth n plays note n (1 = C ... 12 = B)
        ++multis;
        if (firstMulti.isEmpty()) firstMulti = info.meta.name;
        REQUIRE (rig.proc.loadMultiPreset (info.file));
        REQUIRE (rig.proc.getCurrentPresetName() == info.meta.name);
        auto plain = [&rig] (const char* id) { auto* p = rig.proc.getState().getParameter (id); return p->convertFrom0to1 (p->getValue()); };
        REQUIRE (plain (ParamIDs::scaleOn) > 0.5f);
        for (int l = 0; l < numLayers; ++l)
        {
            REQUIRE (plain (ParamIDs::layerOn[(size_t) l]) > 0.5f);
            REQUIRE (juce::roundToInt (plain (ParamIDs::scaleSlot[(size_t) l])) == (1 << l));
            ++perEngine[(size_t) juce::roundToInt (plain (ParamIDs::layerEngine[(size_t) l]))];
        }
        for (int k = 0; k < 12; ++k)       // each note sounds its own synth only
        {
            rig.noteOn (60 + k, 0.8f);
            rig.run ({}, 4);
            REQUIRE (rig.layersPlaying (60 + k) == (1 << k));
            rig.noteOff (60 + k);
        }
    }
    REQUIRE (multis == 24);
    REQUIRE (layers == 128);
    REQUIRE (firstMulti == "01 Bass Multi");
    for (int e = 0; e < 5; ++e) REQUIRE (perEngine[(size_t) e] >= 12);
}

TEST_CASE ("Presets: factory loudness", "[presets][.slow]")
{
    TempDir tmp;
    PresetManager pm (tmp.dir);
    pm.installFactory();
    for (const auto& info : pm.all())
    {
        INFO (info.meta.name.toStdString());
        const auto content = PresetManager::load (info.file);
        const auto m = PresetNormalizer::measure (info.kind, *content);
        REQUIRE (m.truePeak <= PresetNormalizer::ceilingDbtp + 0.05);
        REQUIRE (m.lufs <= PresetNormalizer::targetLufs + 0.5);
        // short percussive sounds are limited by their peak (−1.8 dBTP), not by the 3 s short-term loudness
        if (m.lufs < PresetNormalizer::targetLufs - 8.0)
            REQUIRE (m.truePeak >= PresetNormalizer::ceilingDbtp - 1.5);
    }
}
