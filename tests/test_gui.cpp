#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamIDs.h"

using namespace chroma;

TEST_CASE ("GUI: editor scales 75-200 % and grows up to the screen", "[gui]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    ChromaProcessor proc;
    {
        ChromaEditor ed (proc);
        REQUIRE (ed.getWidth() == ChromaEditor::baseW);
        REQUIRE (ed.currentScale() == 1.0f);

        ed.setSize (960, 600);
        REQUIRE (ed.currentScale() == 0.75f);
        REQUIRE (ed.logicalBounds().getWidth() == ChromaEditor::baseW);

        ed.setSize (2560, 1600);
        REQUIRE (ed.currentScale() == 2.0f);

        // larger than 200 %: scale is clamped, the logical layout grows instead
        ed.setSize (3200, 1800);
        REQUIRE (ed.currentScale() == 2.0f);
        REQUIRE (ed.logicalBounds().getWidth() == 1600);
        REQUIRE (ed.getConstrainer()->getMaximumWidth() >= 2560);
        REQUIRE (ed.getConstrainer()->getMinimumWidth() == 960);

        ed.setSize (1500, 800);   // other aspect ratios: wider layout at 100 %
        REQUIRE (ed.currentScale() == 1.0f);
        REQUIRE (ed.logicalBounds().getWidth() == 1500);
    }
    REQUIRE (proc.editorSize == juce::Point<int> (1500, 800));

    juce::MemoryBlock mb;
    proc.getStateInformation (mb);
    ChromaProcessor other;
    other.setStateInformation (mb.getData(), (int) mb.getSize());
    REQUIRE (other.editorSize == juce::Point<int> (1500, 800));
}

TEST_CASE ("GUI: every engine page builds and follows the selected layer", "[gui]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    ChromaProcessor proc;
    ChromaEditor ed (proc);
    auto& apvts = proc.getState();
    for (int e = 0; e < 5; ++e)
    {
        auto* p = apvts.getParameter ("layer3_engine");
        p->setValueNotifyingHost (p->convertTo0to1 ((float) e));
        ed.showLayer (2);
        ed.tick();
        REQUIRE (ed.engineShown() == e);
        for (int t = 0; t < 6; ++t)
        {
            ed.showTab (t);
            ed.tick();
        }
    }
    ed.showLayer (0);
    ed.tick();
    REQUIRE (ed.engineShown() == 0);

    // a control pass is cheap (message thread only, no audio-thread work)
    ed.showTab (0);
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 60; ++i) ed.tick();
    const double perFrame = (juce::Time::getMillisecondCounterHiRes() - t0) / 60.0;
    WARN ("GUI control refresh: " << perFrame << " ms per pass");
    REQUIRE (perFrame < 8.0);
}

TEST_CASE ("GUI: lock-free meters report layer and master peaks", "[gui]")
{
    ChromaProcessor proc;
    proc.setPlayConfigDetails (0, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    proc.processBlock (buffer, midi);
    midi.clear();
    for (int i = 0; i < 4; ++i) proc.processBlock (buffer, midi);
    auto& s = proc.getSynth();
    REQUIRE (s.takeLayerPeak (0) > 0.001f);
    REQUIRE (s.takeLayerPeak (0) == 0.0f);         // taken = reset until the next block
    REQUIRE (s.takeLayerPeak (5) == 0.0f);
    REQUIRE (s.takeMasterPeak (0) > 0.001f);
    REQUIRE (s.meterVoiceCount (0) == 1);
}

// Dev helper (hidden): writes editor snapshots of every tab to $CHROMA_SNAPSHOT_DIR.
TEST_CASE ("GUI: snapshots", "[.snapshot]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto dir = juce::File (juce::SystemStats::getEnvironmentVariable ("CHROMA_SNAPSHOT_DIR", "/tmp"));
    ChromaProcessor proc;
    ChromaEditor ed (proc);
    static const char* const names[] { "engine", "voice", "mod", "fx", "masterfx", "global" };
    for (int e = 0; e < 5; ++e)
    {
        auto* p = proc.getState().getParameter ("layer1_engine");
        p->setValueNotifyingHost (p->convertTo0to1 ((float) e));
        for (int t = 0; t < (e == 0 ? 6 : 1); ++t)
        {
            ed.showTab (t);
            ed.tick();
            auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat png;
            juce::FileOutputStream out (dir.getChildFile (juce::String ("gui_") + names[t] + "_e" + juce::String (e) + ".png"));
            out.setPosition (0);
            out.truncate();
            png.writeImageToStream (img, out);
        }
    }
    auto shot = [&] (const char* name)
    {
        ed.tick();
        auto im = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat fmt;
        juce::FileOutputStream os (dir.getChildFile (name));
        os.setPosition (0);
        os.truncate();
        fmt.writeImageToStream (im, os);
    };
    ed.setSize (1280, 2400);
    ed.showTab (2);
    shot ("gui_mod_tall.png");
    auto* fm = proc.getState().getParameter ("layer1_engine");
    fm->setValueNotifyingHost (fm->convertTo0to1 (2.0f));
    ed.showTab (0);
    shot ("gui_fm_tall.png");
    ed.setSize (960, 600);
    ed.tick();
    auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
    juce::PNGImageFormat png;
    juce::FileOutputStream out (dir.getChildFile ("gui_small.png"));
    out.setPosition (0);
    out.truncate();
    png.writeImageToStream (img, out);
}
