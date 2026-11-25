#include <catch2/catch_test_macros.hpp>
#include "TestHelpers.h"
#include "shared/Scale.h"

using namespace test;
namespace ids = chroma::ParamIDs;

TEST_CASE ("SCALE mapping function", "[scale]")
{
    uint16_t masks[12] {};
    masks[0] = 0b001;   // C  -> layer 1
    masks[2] = 0b110;   // D  -> layers 2,3
    const uint16_t eligible = 0b011;

    REQUIRE (chroma::scaleLayersForNote (60, false, masks, eligible) == eligible);   // Scale off: all layers
    REQUIRE (chroma::scaleLayersForNote (60, true, masks, eligible) == 0b001);
    REQUIRE (chroma::scaleLayersForNote (36, true, masks, eligible) == 0b001);       // every octave
    REQUIRE (chroma::scaleLayersForNote (62, true, masks, eligible) == 0b010);       // only eligible layers
    REQUIRE (chroma::scaleLayersForNote (64, true, masks, eligible) == 0);           // empty slot = mute
    REQUIRE (chroma::scaleLayersForNote (1, true, masks, 0xFFF) == 0);
}

TEST_CASE ("SCALE assigns note classes to layers", "[scale]")
{
    Rig r;
    setParam (r.proc, ids::layerOn[1], 1.0f);
    setParam (r.proc, ids::layerOn[2], 1.0f);
    setParam (r.proc, ids::scaleOn, 1.0f);
    for (int i = 0; i < 12; ++i)
        setParam (r.proc, ids::scaleSlot[(size_t) i], 0.0f);
    setParam (r.proc, ids::scaleSlot[0], 0b001);   // C
    setParam (r.proc, ids::scaleSlot[2], 0b110);   // D
    setParam (r.proc, ids::scaleSlot[4], 0b111);   // E
    r.run();

    r.noteOn (60); r.noteOn (74); r.noteOn (40); r.noteOn (65);
    REQUIRE (r.layersPlaying (60) == 0b001);
    REQUIRE (r.layersPlaying (74) == 0b110);
    REQUIRE (r.layersPlaying (40) == 0b111);
    REQUIRE (r.layersPlaying (65) == 0);              // F: empty slot -> muted

    SECTION ("Scale off = layering")
    {
        setParam (r.proc, ids::scaleOn, 0.0f);
        r.noteOn (66);
        REQUIRE (r.layersPlaying (66) == 0b111);
    }
    SECTION ("Layer off never plays")
    {
        setParam (r.proc, ids::layerOn[2], 0.0f);
        r.noteOn (52);
        REQUIRE (r.layersPlaying (52) == 0b011);
    }
}

TEST_CASE ("Voice stealing: oldest first", "[voices]")
{
    Rig r;
    setParam (r.proc, ids::masterPolyphony, 0.0f);    // 12 voices
    r.run();
    REQUIRE (r.synth().poolSize() == 12);
    for (int n = 60; n < 72; ++n) r.noteOn (n);
    REQUIRE (r.synth().activeVoiceCount() == 12);

    r.noteOn (72);
    REQUIRE (r.synth().activeVoiceCount() == 12);
    REQUIRE_FALSE (r.notePlaying (60));
    REQUIRE (r.notePlaying (61));
    REQUIRE (r.notePlaying (72));

    r.run ({}, 10);     // the stolen voice crossfades into the new note
    REQUIRE (r.notePlaying (72));
    REQUIRE (r.synth().activeVoiceCount() == 12);
}

TEST_CASE ("Voice stealing: same age -> quietest", "[voices]")
{
    Rig r;
    setParam (r.proc, ids::masterPolyphony, 0.0f);
    r.synth().layerStore (0).set (chroma::lp::env1Velocity, 1.0f);
    r.run();

    juce::MidiBuffer chord;
    chord.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    chord.addEvent (juce::MidiMessage::noteOn (1, 61, 0.1f), 0);
    r.run (chord, 20);
    for (int n = 62; n < 72; ++n) r.noteOn (n);
    r.noteOn (72);
    REQUIRE (r.notePlaying (60));
    REQUIRE_FALSE (r.notePlaying (61));
    REQUIRE (r.notePlaying (72));
}

TEST_CASE ("Layer Max Voices and pool limits", "[voices]")
{
    Rig r;
    setParam (r.proc, ids::layerMaxVoices[0], 4.0f);
    r.run();
    for (int n = 60; n < 66; ++n) r.noteOn (n);
    REQUIRE (r.synth().activeVoiceCount() == 4);
    REQUIRE_FALSE (r.notePlaying (60));
    REQUIRE (r.notePlaying (65));

    SECTION ("stealing off drops new notes")
    {
        r.synth().layerStore (0).set (chroma::lp::voiceSteal, 0.0f);
        r.run();
        r.noteOn (70);
        REQUIRE_FALSE (r.notePlaying (70));
    }
}

TEST_CASE ("Voices are freed after release", "[voices]")
{
    Rig r;
    r.noteOn (60);
    r.noteOff (60);
    r.run ({}, 1000);
    REQUIRE (r.synth().activeVoiceCount() == 0);
}

TEST_CASE ("Sustain pedal holds released notes", "[voices]")
{
    Rig r;
    juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
    r.run (m);
    r.noteOn (60);
    r.noteOff (60);
    r.run ({}, 400);
    REQUIRE (r.notePlaying (60));
    m.clear();
    m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0);
    r.run (m, 1000);
    REQUIRE_FALSE (r.notePlaying (60));
}

TEST_CASE ("Mono and legato use one voice per layer", "[voices]")
{
    Rig r;
    r.synth().layerStore (0).set (chroma::lp::voiceMode, 3.0f);   // Legato
    r.run();
    r.noteOn (60);
    r.noteOn (64);
    REQUIRE (r.synth().activeVoiceCount() == 1);
    REQUIRE (r.notePlaying (64));
    r.noteOff (64);
    REQUIRE (r.notePlaying (60));                                   // returns to the held note
}

TEST_CASE ("MPE member channels play per-note", "[mpe]")
{
    Rig r;
    setParam (r.proc, ids::mpeOn, 1.0f);
    setParam (r.proc, ids::layerMidiChannel[0], 1.0f);              // channel filter ignored in MPE
    r.run();
    r.noteOn (60, 0.8f, 2);
    r.noteOn (64, 0.8f, 3);
    REQUIRE (r.notePlaying (60));
    REQUIRE (r.notePlaying (64));
    r.noteOff (60, 2);
    r.run ({}, 1000);
    REQUIRE_FALSE (r.notePlaying (60));
    REQUIRE (r.notePlaying (64));
}

TEST_CASE ("Automation slot and macro drive internal parameters", "[automation]")
{
    Rig r;
    const int slot = r.synth().assignAutomation (0, chroma::lp::pitch);
    REQUIRE (slot == 0);
    REQUIRE (r.synth().assignAutomation (0, chroma::lp::pitch) == 0);   // stable slot
    REQUIRE (r.synth().assignMacro (0, { chroma::ParamTarget::host, r.synth().hostParamIndex (ids::masterVolume) }, 0.0f, 1.0f));

    juce::MemoryBlock mb;
    r.proc.getStateInformation (mb);
    chroma::ChromaProcessor b;
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    REQUIRE (b.getSynth().getAutomationTarget (0) == chroma::ParamTarget { 0, chroma::lp::pitch });
    REQUIRE (b.getSynth().getMacroMap().count == 1);
}
