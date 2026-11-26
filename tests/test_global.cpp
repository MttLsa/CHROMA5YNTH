// FASE 9: Preset (§6, without factory presets), Global settings and MIDI (§5).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "TestHelpers.h"
#include "shared/Tuning.h"
#include "core/Randomizer.h"
#include "shared/ParamRange.h"

using Catch::Matchers::WithinAbs;
using namespace chroma;

namespace
{
struct TempDir
{
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("chroma_presets_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    TempDir() { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }
};

float hostPlain (ChromaProcessor& p, const char* id)
{
    auto* prm = p.getState().getParameter (id);
    return prm->convertFrom0to1 (prm->getValue());
}

// Upward zero crossings per second of the left output over `seconds` after a short attack.
double measureHz (test::Rig& r, int note, double seconds = 1.0)
{
    r.noteOn (note, 0.9f);
    r.run ({}, 40);
    int crossings = 0;
    float prev = 0.0f;
    const int blocks = (int) (seconds * 48000.0 / 64.0);
    for (int b = 0; b < blocks; ++b)
    {
        r.run();
        for (int i = 0; i < 64; ++i)
        {
            const float x = r.buffer.getSample (0, i);
            if (prev <= 0.0f && x > 0.0f) ++crossings;
            prev = x;
        }
    }
    return crossings / (blocks * 64.0 / 48000.0);
}
} // namespace

// ------------------------------------------------------------------------------------------------ tuning
TEST_CASE ("Tuning: .scl parsing (cents, ratios, comments)", "[global][tuning]")
{
    const auto sc = tuning::parseScl ("! test.scl\n!\nJust major\n 3\n!\n 5/4\n 701.955 cents\n 2\n");
    REQUIRE (sc.has_value());
    REQUIRE (sc->description == "Just major");
    REQUIRE (sc->cents.size() == 3);
    REQUIRE_THAT (sc->cents[0], WithinAbs (386.3137, 1e-3));
    REQUIRE_THAT (sc->cents[1], WithinAbs (701.955, 1e-6));
    REQUIRE_THAT (sc->cents[2], WithinAbs (1200.0, 1e-9));
    REQUIRE_FALSE (tuning::parseScl ("bad\n5\n100.0\n").has_value());
    REQUIRE_FALSE (tuning::parseScl ("x\n2\n0/3\n2/1\n").has_value());
}

TEST_CASE ("Tuning: 12-TET .scl is identity, A4 = 440", "[global][tuning]")
{
    juce::String text = "12-TET\n12\n";
    for (int i = 1; i <= 12; ++i) text << juce::String (i * 100.0, 1) << "\n";
    const auto t = tuning::fromScale (*tuning::parseScl (text));
    for (auto s : t.semis) REQUIRE_THAT (s, WithinAbs (0.0f, 1e-4f));
}

TEST_CASE ("Tuning: non-12 scale + .kbm mapping", "[global][tuning]")
{
    // 19-EDO, default mapping: note 69 = 440 Hz, one key per degree.
    juce::String text = "19-EDO\n19\n";
    for (int i = 1; i <= 19; ++i) text << juce::String (i * 1200.0 / 19.0, 6) << "\n";
    const auto sc = *tuning::parseScl (text);
    const auto t = tuning::fromScale (sc);
    REQUIRE_THAT (69.0f + t.semis[69], WithinAbs (69.0f, 1e-4f));
    REQUIRE_THAT (70.0f + t.semis[70], WithinAbs (69.0f + 12.0f / 19.0f, 1e-4f));
    REQUIRE_THAT (88.0f + t.semis[88], WithinAbs (81.0f, 1e-3f));   // 19 keys up = one octave

    // .kbm: 12-key map, white keys only (black keys unmapped -> 12-TET), reference A4 = 432 Hz.
    const auto km = tuning::parseKbm ("! map\n12\n0\n127\n60\n69\n432.0\n7\n0\nx\n1\nx\n2\n3\nx\n4\nx\n5\nx\n6\n");
    REQUIRE (km.has_value());
    juce::String dia = "Diatonic\n7\n";
    for (double c : { 200.0, 400.0, 500.0, 700.0, 900.0, 1100.0, 1200.0 }) dia << juce::String (c, 1) << "\n";
    const auto t2 = tuning::fromScale (*tuning::parseScl (dia), km);
    const float ref = 69.0f + 12.0f * std::log2 (432.0f / 440.0f);
    REQUIRE_THAT (69.0f + t2.semis[69], WithinAbs (ref, 1e-3f));
    REQUIRE_THAT (60.0f + t2.semis[60], WithinAbs (ref - 9.0f, 1e-3f));   // A -> C = 900 cents down
    REQUIRE_THAT (t2.semis[61], WithinAbs (0.0f, 1e-6f));                 // unmapped key
    REQUIRE_THAT (72.0f + t2.semis[72], WithinAbs (ref + 3.0f, 1e-3f));
}

TEST_CASE ("Tuning: .tun parsing", "[global][tuning]")
{
    const auto t = tuning::parseTun ("; AnaMark\n[Tuning]\nnote 69 = 6950\nnote 60=6000\n[Exact Tuning]\nnote 60 = 6010.5\n");
    REQUIRE (t.has_value());
    REQUIRE_THAT (t->semis[69], WithinAbs (0.5f, 1e-4f));
    REQUIRE_THAT (t->semis[60], WithinAbs (0.105f, 1e-4f));   // [Exact Tuning] wins
    REQUIRE_THAT (t->semis[61], WithinAbs (0.0f, 1e-6f));
    const auto b = tuning::parseTun ("[Exact Tuning]\nBaseFreq = 16.3515978313\nnote 0 = 0\n");
    REQUIRE_THAT (b->semis[0], WithinAbs (12.0f, 1e-4f));
    REQUIRE_FALSE (tuning::parseTun ("[Info]\nName=x\n").has_value());
}

TEST_CASE ("Master tune and microtuning change the played pitch", "[global][tuning]")
{
    test::Rig r;
    const double base = measureHz (r, 69);
    REQUIRE_THAT (base, WithinAbs (440.0, 3.0));
    r.noteOff (69);
    r.run ({}, 200);

    test::setParam (r.proc, ParamIDs::masterTune, 466.0f);
    REQUIRE_THAT (r.synth().tunedPitch (69), WithinAbs (69.0f + 12.0f * std::log2 (466.0f / 440.0f), 1e-4f));
    REQUIRE_THAT (measureHz (r, 69), WithinAbs (466.0, 3.0));
    r.noteOff (69);
    r.run ({}, 200);

    TuningTable t;
    t.semis[69] = 12.0f;   // A4 plays A5 (with master tune 466 -> 932 Hz)
    r.synth().setTuning (t, "Test");
    REQUIRE_THAT (measureHz (r, 69), WithinAbs (932.0, 5.0));

    // Tuning is global state: saved with the project, not in Multi presets.
    auto core = r.synth().saveState (true);
    REQUIRE (core.getChildWithName ("TUNING").isValid());
    REQUIRE_FALSE (r.synth().saveState (false).getChildWithName ("TUNING").isValid());
    test::Rig r2;
    r2.synth().loadState (core);
    REQUIRE (r2.synth().getTuningName() == "Test");
    REQUIRE_THAT (r2.synth().tunedPitch (69), WithinAbs (81.0f, 1e-4f));
}

TEST_CASE ("Tuning files load by extension (.scl + sibling .kbm, .tun)", "[global][tuning]")
{
    TempDir tmp;
    auto scl = tmp.dir.getChildFile ("quarter.scl");
    juce::String text = "24-EDO\n24\n";
    for (int i = 1; i <= 24; ++i) text << juce::String (i * 50.0, 1) << "\n";
    scl.replaceWithText (text);
    test::Rig r;
    REQUIRE (r.synth().loadTuningFile (scl));
    REQUIRE (r.synth().getTuningName() == "quarter");
    REQUIRE_THAT (r.synth().tunedPitch (70), WithinAbs (69.5f, 1e-4f));
    auto tun = tmp.dir.getChildFile ("x.tun");
    tun.replaceWithText ("[Tuning]\nnote 60 = 6100\n");
    REQUIRE (r.synth().loadTuningFile (tun));
    REQUIRE_THAT (r.synth().tunedPitch (60), WithinAbs (61.0f, 1e-4f));
    juce::String err;
    auto bad = tmp.dir.getChildFile ("bad.scl");
    bad.replaceWithText ("nothing");
    REQUIRE_FALSE (r.synth().loadTuningFile (bad, &err));
    REQUIRE (err.isNotEmpty());
}

// ------------------------------------------------------------------------------------------------ oversampling
TEST_CASE ("Oversampling: separate realtime / offline factors", "[global]")
{
    test::Rig r;
    r.run();
    REQUIRE (r.synth().currentOversampling() == 2);
    test::setParam (r.proc, ParamIDs::oversamplingRealtime, 3.0f);   // 8x
    r.run();
    REQUIRE (r.synth().currentOversampling() == 8);
    r.proc.setNonRealtime (true);
    r.run();
    REQUIRE (r.synth().currentOversampling() == 4);                  // offline default 4x
    test::setParam (r.proc, ParamIDs::oversamplingOffline, 0.0f);
    r.noteOn (60);
    r.run ({}, 20);
    REQUIRE (r.synth().currentOversampling() == 1);
    for (int i = 0; i < 64; ++i) REQUIRE (std::isfinite (r.buffer.getSample (0, i)));
}

// ------------------------------------------------------------------------------------------------ MIDI learn
TEST_CASE ("MIDI learn: layer and host parameters, CC mapping saved", "[global][midi]")
{
    test::Rig r;
    auto& s = r.synth();
    const ParamTarget cutoff { 0, lp::filterCutoff };
    s.startMidiLearn (cutoff);
    REQUIRE (s.isMidiLearning());
    juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::controllerEvent (3, 21, 10), 0);
    r.run (m);
    m.clear();
    REQUIRE_FALSE (s.isMidiLearning());
    REQUIRE (s.pollMidiLearn());
    REQUIRE (s.midiMappingFor (cutoff) == 21);

    m.addEvent (juce::MidiMessage::controllerEvent (1, 21, 127), 0);
    r.run (m);
    m.clear();
    REQUIRE_THAT (s.layerStore (0).get (lp::filterCutoff), WithinAbs (lp::descs[(size_t) lp::filterCutoff].max, 1e-2f));
    m.addEvent (juce::MidiMessage::controllerEvent (5, 21, 0), 0);
    r.run (m);
    m.clear();
    REQUIRE_THAT (s.layerStore (0).get (lp::filterCutoff), WithinAbs (lp::descs[(size_t) lp::filterCutoff].min, 1e-2f));

    // Host parameter: immediate on the audio side, pushed to the host by the message thread.
    const ParamTarget vol { ParamTarget::host, s.hostParamIndex (ParamIDs::masterVolume) };
    s.startMidiLearn (vol);
    m.addEvent (juce::MidiMessage::controllerEvent (1, 7, 64), 0);
    r.run (m);
    m.clear();
    REQUIRE (s.pollMidiLearn());
    m.addEvent (juce::MidiMessage::controllerEvent (1, 7, 0), 0);
    r.run (m);
    m.clear();
    s.flushMidiToHost();
    REQUIRE_THAT (r.proc.getState().getParameter (ParamIDs::masterVolume)->getValue(), WithinAbs (0.0f, 1e-6f));

    // Re-learning a CC moves it to the new target.
    const ParamTarget res { 0, lp::filterResonance };
    s.startMidiLearn (res);
    m.addEvent (juce::MidiMessage::controllerEvent (1, 21, 1), 0);
    r.run (m);
    m.clear();
    REQUIRE (s.pollMidiLearn());
    REQUIRE (s.midiMappingFor (cutoff) == -1);
    REQUIRE (s.midiMappingFor (res) == 21);
    REQUIRE (s.getMidiMap().count == 2);

    // Saved with the project and as a file.
    TempDir tmp;
    const auto f = tmp.dir.getChildFile ("map.c5midi");
    REQUIRE (s.saveMidiMap (f));
    juce::MemoryBlock mb;
    r.proc.getStateInformation (mb);
    test::Rig r2;
    r2.proc.setStateInformation (mb.getData(), (int) mb.getSize());
    REQUIRE (r2.synth().midiMappingFor (res) == 21);
    REQUIRE (r2.synth().midiMappingFor (vol) == 7);
    r2.synth().clearMidiMap();
    REQUIRE (r2.synth().getMidiMap().count == 0);
    REQUIRE (r2.synth().loadMidiMap (f));
    REQUIRE (r2.synth().midiMappingFor (vol) == 7);
    r2.synth().removeMidiMapping (vol);
    REQUIRE (r2.synth().midiMappingFor (vol) == -1);

    // Channel-specific mapping.
    REQUIRE (r2.synth().addMidiMapping (30, 1, { 0, lp::filterDrive }));
    juce::MidiBuffer m2;
    m2.addEvent (juce::MidiMessage::controllerEvent (1, 30, 127), 0);
    r2.run (m2);
    m2.clear();
    const float drive0 = r2.synth().layerStore (0).get (lp::filterDrive);
    REQUIRE_THAT (drive0, WithinAbs (lp::descs[(size_t) lp::filterDrive].def, 1e-6f));
    m2.addEvent (juce::MidiMessage::controllerEvent (2, 30, 127), 0);
    r2.run (m2);
    m2.clear();
    REQUIRE_THAT (r2.synth().layerStore (0).get (lp::filterDrive), WithinAbs (lp::descs[(size_t) lp::filterDrive].max, 1e-3f));
}

// ------------------------------------------------------------------------------------------------ undo / A-B
TEST_CASE ("Undo/Redo covers host and internal parameters", "[global][history]")
{
    ChromaProcessor p;
    auto& s = p.getSynth();
    test::setParam (p, ParamIDs::masterVolume, -12.0f);
    p.commitHistory();
    s.layerStore (0).set (lp::filterCutoff, 300.0f);
    p.commitHistory();
    s.assignMacro (0, { 0, lp::filterResonance }, 0.0f, 1.0f);
    p.commitHistory();
    REQUIRE (p.getHistory().canUndo());

    REQUIRE (p.undo());
    REQUIRE (s.getMacroMap().count == 0);
    REQUIRE_THAT (s.layerStore (0).get (lp::filterCutoff), WithinAbs (300.0f, 1e-3f));
    REQUIRE (p.undo());
    REQUIRE_THAT (s.layerStore (0).get (lp::filterCutoff), WithinAbs (lp::descs[(size_t) lp::filterCutoff].def, 1e-3f));
    REQUIRE_THAT (hostPlain (p, ParamIDs::masterVolume), WithinAbs (-12.0f, 0.02f));
    REQUIRE (p.undo());
    REQUIRE_THAT (hostPlain (p, ParamIDs::masterVolume), WithinAbs (0.0f, 0.02f));
    REQUIRE_FALSE (p.undo());

    REQUIRE (p.redo());
    REQUIRE (p.redo());
    REQUIRE_THAT (s.layerStore (0).get (lp::filterCutoff), WithinAbs (300.0f, 1e-3f));
    // A new edit drops the redo stack.
    s.layerStore (0).set (lp::filterDrive, 0.7f);
    REQUIRE_FALSE (p.redo());
    REQUIRE (p.undo());
    REQUIRE_THAT (s.layerStore (0).get (lp::filterDrive), WithinAbs (lp::descs[(size_t) lp::filterDrive].def, 1e-4f));

    // Global settings are not part of the history.
    test::setParam (p, ParamIDs::masterTune, 432.0f);
    p.undo();
    REQUIRE_THAT (hostPlain (p, ParamIDs::masterTune), WithinAbs (432.0f, 0.01f));
}

TEST_CASE ("Undo checkpoints are taken by idle() after edits settle", "[global][history]")
{
    ChromaProcessor p;
    p.getSynth().layerStore (2).set (lp::filterCutoff, 1000.0f);
    p.idle();                                   // edit seen, not yet settled
    REQUIRE_FALSE (p.getHistory().canUndo());
    juce::Thread::sleep (450);
    p.idle();
    REQUIRE (p.getHistory().canUndo());
}

TEST_CASE ("A/B compare keeps two independent states", "[global][history]")
{
    ChromaProcessor p;
    auto& s = p.getSynth();
    s.layerStore (0).set (lp::filterCutoff, 500.0f);
    p.selectAB (1);                              // B starts as a copy of A
    REQUIRE (p.getHistory().activeSlot() == 1);
    REQUIRE_THAT (s.layerStore (0).get (lp::filterCutoff), WithinAbs (500.0f, 1e-3f));
    s.layerStore (0).set (lp::filterCutoff, 4000.0f);
    test::setParam (p, ParamIDs::masterVolume, -6.0f);
    p.selectAB (0);
    REQUIRE_THAT (s.layerStore (0).get (lp::filterCutoff), WithinAbs (500.0f, 1e-3f));
    REQUIRE_THAT (hostPlain (p, ParamIDs::masterVolume), WithinAbs (0.0f, 0.02f));
    p.selectAB (1);
    REQUIRE_THAT (s.layerStore (0).get (lp::filterCutoff), WithinAbs (4000.0f, 1e-2f));
    REQUIRE_THAT (hostPlain (p, ParamIDs::masterVolume), WithinAbs (-6.0f, 0.02f));
    p.getHistory().copyActiveToOther();
    p.selectAB (0);
    REQUIRE_THAT (s.layerStore (0).get (lp::filterCutoff), WithinAbs (4000.0f, 1e-2f));
    REQUIRE (p.undo());                          // A/B switches are undoable
}

// ------------------------------------------------------------------------------------------------ randomize / init
TEST_CASE ("Randomize: scopes, amount, protected parameters", "[global][random]")
{
    ChromaProcessor p;
    auto& s = p.getSynth();
    const auto before = p.captureState (true);
    REQUIRE (p.randomize ({ RandomizeScope::Global }, 0.0f, 1) == 0);

    // Layer scope touches only that layer's internals (+ pan); protected host params untouched.
    REQUIRE (p.randomize ({ RandomizeScope::Layer, 3 }, 1.0f, 42) > 50);
    int changedOther = 0;
    for (int i = 0; i < lp::count; ++i)
        if (s.layerStore (0).get (i) != lp::descs[(size_t) i].def) ++changedOther;
    REQUIRE (changedOther == 0);
    int changed3 = 0;
    for (int i = 0; i < lp::count; ++i)
        if (s.layerStore (3).get (i) != lp::descs[(size_t) i].def) ++changed3;
    REQUIRE (changed3 > 50);
    REQUIRE (s.layerStore (3).get (lp::voiceMode) == lp::descs[(size_t) lp::voiceMode].def);   // Layer · Voice protected
    REQUIRE_THAT (hostPlain (p, ParamIDs::masterVolume), WithinAbs (0.0f, 1e-3f));
    REQUIRE (hostPlain (p, ParamIDs::layerVolume[3]) == p.getState().getParameter (ParamIDs::layerVolume[3])->convertFrom0to1 (
                 p.getState().getParameter (ParamIDs::layerVolume[3])->getDefaultValue()));
    REQUIRE (p.undo());
    {
        const auto after = p.captureState (true);
        for (int i = 0; i < before.getNumChildren(); ++i)
            if (! before.getChild (i).isEquivalentTo (after.getChild (i)))
                UNSCOPED_INFO (before.getChild (i).toXmlString() << " vs " << after.getChild (i).toXmlString());
        REQUIRE (after.isEquivalentTo (before));
    }

    // Small amount = small moves of continuous parameters.
    s.layerStore (0).set (lp::filterCutoff, 1000.0f);
    const float n0 = paramRange (lp::filterCutoff).toNorm (1000.0f);
    p.randomize ({ RandomizeScope::Section, 0, juce::String::fromUTF8 ("Layer · Filter") }, 0.1f, 7);
    REQUIRE (std::abs (paramRange (lp::filterCutoff).toNorm (s.layerStore (0).get (lp::filterCutoff)) - n0) <= 0.1f + 1e-4f);
    REQUIRE (s.layerStore (0).get (lp::env1Attack) == lp::descs[(size_t) lp::env1Attack].def);  // other section untouched

    // Section prefix + host section; Global only randomizes layers that are on.
    p.randomize ({ RandomizeScope::Section, 1, "Analog" }, 1.0f, 9);
    REQUIRE (s.layerStore (1).get (lp::filterCutoff) == lp::descs[(size_t) lp::filterCutoff].def);
    const float rev = p.getState().getParameter (ParamIDs::mfxReverbSize)->getValue();
    p.randomize ({ RandomizeScope::Section, 0, juce::String::fromUTF8 ("Master FX · Reverb") }, 1.0f, 11);
    REQUIRE (p.getState().getParameter (ParamIDs::mfxReverbSize)->getValue() != rev);
    p.randomize ({ RandomizeScope::Global }, 1.0f, 13);
    for (int i = 0; i < lp::count; ++i)
        REQUIRE (s.layerStore (5).get (i) == lp::descs[(size_t) i].def);   // layer 6 is off
    REQUIRE (Randomizer::sectionMatches (juce::String::fromUTF8 ("Analog · OSC 1"), "Analog"));
    REQUIRE_FALSE (Randomizer::sectionMatches ("Analogue", "Analog"));
    REQUIRE (Randomizer::layerSections().contains (juce::String::fromUTF8 ("Layer · Filter")));
}

TEST_CASE ("Init resets the patch, keeps Global settings", "[global]")
{
    ChromaProcessor p;
    auto& s = p.getSynth();
    test::setParam (p, ParamIDs::masterVolume, -20.0f);
    test::setParam (p, ParamIDs::masterTune, 450.0f);
    s.layerStore (4).set (lp::filterCutoff, 100.0f);
    s.assignMacro (1, { 4, lp::filterCutoff }, 0.0f, 1.0f);
    s.addMidiMapping (10, -1, { 0, lp::filterCutoff });
    p.initAll();
    REQUIRE_THAT (hostPlain (p, ParamIDs::masterVolume), WithinAbs (0.0f, 1e-3f));
    REQUIRE_THAT (hostPlain (p, ParamIDs::masterTune), WithinAbs (450.0f, 0.01f));
    REQUIRE (s.layerStore (4).get (lp::filterCutoff) == lp::descs[(size_t) lp::filterCutoff].def);
    REQUIRE (s.getMacroMap().count == 0);
    REQUIRE (s.getMidiMap().count == 1);
    REQUIRE (p.getCurrentPresetName() == "Init");
    REQUIRE (p.undo());
    REQUIRE_THAT (hostPlain (p, ParamIDs::masterVolume), WithinAbs (-20.0f, 0.02f));
}

// ------------------------------------------------------------------------------------------------ presets
TEST_CASE ("Presets: Multi save/load keeps Global settings", "[global][preset]")
{
    TempDir tmp;
    ChromaProcessor p;
    p.setPresetRoot (tmp.dir);
    auto& s = p.getSynth();
    test::setParam (p, ParamIDs::masterVolume, -9.0f);
    test::setParam (p, ParamIDs::layerOn[2], 1.0f);
    s.layerStore (2).set (lp::filterCutoff, 777.0f);
    s.assignMacro (3, { 2, lp::filterCutoff }, 0.2f, 0.8f);
    const auto f = p.saveMultiPreset ({ "Glass Pad", "Pad", "Me", { "warm", "wide" } });
    REQUIRE (f.existsAsFile());
    REQUIRE (f.hasFileExtension (PresetManager::multiExt));
    REQUIRE (p.getCurrentPresetName() == "Glass Pad");

    p.initAll();
    test::setParam (p, ParamIDs::masterTune, 435.0f);
    s.addMidiMapping (1, -1, { 0, lp::filterDrive });
    REQUIRE (p.loadMultiPreset (f));
    REQUIRE_THAT (hostPlain (p, ParamIDs::masterVolume), WithinAbs (-9.0f, 0.02f));
    REQUIRE (hostPlain (p, ParamIDs::layerOn[2]) > 0.5f);
    REQUIRE_THAT (s.layerStore (2).get (lp::filterCutoff), WithinAbs (777.0f, 1e-2f));
    REQUIRE (s.getMacroMap().count == 1);
    REQUIRE_THAT (hostPlain (p, ParamIDs::masterTune), WithinAbs (435.0f, 0.01f));   // Global kept
    REQUIRE (s.getMidiMap().count == 1);                                              // MIDI map kept
    REQUIRE (p.getCurrentPresetName() == "Glass Pad");
    REQUIRE (p.undo());                                                               // loading is undoable
    REQUIRE_FALSE (p.loadLayerPreset (0, f));                                          // wrong kind
}

TEST_CASE ("Presets: Layer preset loads into any slot", "[global][preset]")
{
    TempDir tmp;
    ChromaProcessor p;
    p.setPresetRoot (tmp.dir);
    auto& s = p.getSynth();
    test::setParam (p, ParamIDs::layerEngine[0], 2.0f);
    s.layerStore (0).set (lp::filterResonance, 0.66f);
    const auto f = p.saveLayerPreset (0, { "FM Bell", "Bell", "", {} });
    REQUIRE (f.hasFileExtension (PresetManager::layerExt));
    REQUIRE (p.loadLayerPreset (7, f));
    REQUIRE_THAT (hostPlain (p, ParamIDs::layerEngine[7]), WithinAbs (2.0f, 1e-4f));
    REQUIRE_THAT (s.layerStore (7).get (lp::filterResonance), WithinAbs (0.66f, 1e-4f));
    REQUIRE_FALSE (p.loadMultiPreset (f));
    // Raw files written by Synth::saveLayerPreset are accepted too.
    const auto raw = tmp.dir.getChildFile ("raw.c5layer");
    REQUIRE (s.saveLayerPreset (0, raw));
    REQUIRE (p.loadLayerPreset (9, raw));
    REQUIRE_THAT (s.layerStore (9).get (lp::filterResonance), WithinAbs (0.66f, 1e-4f));
}

TEST_CASE ("Presets: browser search, categories, tags, favourites, rename, delete, import/export", "[global][preset]")
{
    TempDir tmp;
    PresetManager pm (tmp.dir.getChildFile ("lib"));
    const juce::ValueTree multi ("CHROMA5YNTH"), layer ("LAYER_PRESET");
    const auto a = pm.save (PresetKind::Multi, multi, { "Deep Sub", "Bass", "Ann", { "dark", "mono" } });
    const auto b = pm.save (PresetKind::Multi, multi, { "Bright Lead", "Lead", "Bob", { "bright" } });
    pm.save (PresetKind::Multi, multi, { "Dark Pad", "Pad", "Ann", { "dark", "wide" } });
    pm.save (PresetKind::Layer, layer, { "Dark Pluck", "Pluck", "Ann", { "dark" } });
    REQUIRE (pm.all().size() == 4);

    using F = PresetManager::Filter;
    REQUIRE (pm.filter (F { PresetKind::Multi }).size() == 3);
    REQUIRE (pm.filter (F { PresetKind::Layer }).size() == 1);
    REQUIRE (pm.filter (F { PresetKind::Multi, "dark" }).size() == 2);          // name or tag
    REQUIRE (pm.filter (F { PresetKind::Multi, "dark ann pad" }).size() == 1);  // all words
    REQUIRE (pm.filter (F { PresetKind::Multi, "", "Lead" }).size() == 1);
    REQUIRE (pm.filter (F { PresetKind::Multi, "", "", "wide" }).size() == 1);
    REQUIRE (pm.filter (F { PresetKind::Multi, "", "", "", "Ann" }).size() == 2);
    REQUIRE (pm.categories (PresetKind::Multi).contains ("Bass"));
    REQUIRE (pm.tags (PresetKind::Multi) == juce::StringArray { "bright", "dark", "mono", "wide" });
    REQUIRE (pm.authors (PresetKind::Multi) == juce::StringArray { "Ann", "Bob" });
    REQUIRE (pm.all().front().meta.name == "Bright Lead");                      // sorted by name

    pm.setFavourite (a, true);
    F fav { PresetKind::Multi };
    fav.favouritesOnly = true;
    REQUIRE (pm.filter (fav).size() == 1);
    pm.rescan();
    REQUIRE (pm.filter (fav).size() == 1);                                       // persisted

    const auto a2 = pm.rename (a, "Deeper Sub");
    REQUIRE (a2.existsAsFile());
    REQUIRE_FALSE (a.exists());
    REQUIRE (pm.isFavourite (a2));
    PresetInfo info;
    REQUIRE (PresetManager::load (a2, &info).has_value());
    REQUIRE (info.meta.name == "Deeper Sub");
    REQUIRE (info.meta.tags == juce::StringArray { "dark", "mono" });

    // Same name, no overwrite -> unique file.
    const auto b2 = pm.save (PresetKind::Multi, multi, { "Bright Lead", "Lead", "", {} }, false);
    REQUIRE (b2 != b);
    REQUIRE (pm.filter (F { PresetKind::Multi, "bright" }).size() == 2);
    REQUIRE (pm.remove (b2));
    REQUIRE (pm.filter (F { PresetKind::Multi, "bright" }).size() == 1);
    REQUIRE_FALSE (pm.remove (tmp.dir.getChildFile ("outside.c5multi")));

    const auto out = tmp.dir.getChildFile ("exported.c5multi");
    REQUIRE (pm.exportFile (b, out));
    REQUIRE (pm.remove (b));
    const auto imp = pm.importFile (out);
    REQUIRE (imp.isAChildOf (pm.folder (PresetKind::Multi)));
    REQUIRE (pm.filter (F { PresetKind::Multi, "bright" }).size() == 1);
    REQUIRE_FALSE (pm.importFile (tmp.dir.getChildFile ("missing.c5multi")).exists());

    REQUIRE (pm.updateMeta (imp, { "Bright Lead", "FX", "Bob", { "new" } }));
    REQUIRE (pm.filter (F { PresetKind::Multi, "", "FX" }).size() == 1);
    REQUIRE (PresetManager::safeFileName ("a/b:c") .isNotEmpty());
    REQUIRE (PresetManager::safeFileName ("  ") == "Untitled");
}
