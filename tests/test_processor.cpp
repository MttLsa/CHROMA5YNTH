#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamIDs.h"

TEST_CASE ("Identity strings", "[processor]")
{
    chroma::ChromaProcessor proc;
    REQUIRE (proc.getName() == "CHROMA5YNTH");
    REQUIRE (chroma::ChromaEditor::aboutText().contains ("TMFU audiovibe"));
}

TEST_CASE ("Process produces finite output", "[processor]")
{
    chroma::ChromaProcessor proc;
    proc.setPlayConfigDetails (0, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);

    for (int i = 0; i < 20; ++i)
    {
        proc.processBlock (buffer, midi);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int s = 0; s < buffer.getNumSamples(); ++s)
                REQUIRE (std::isfinite (buffer.getSample (ch, s)));
    }
    proc.releaseResources();
}

TEST_CASE ("State round-trip", "[processor]")
{
    chroma::ChromaProcessor a, b;
    a.getState().getParameter (chroma::ParamIDs::masterVolume)->setValueNotifyingHost (0.25f);

    juce::MemoryBlock mb;
    a.getStateInformation (mb);
    b.setStateInformation (mb.getData(), (int) mb.getSize());

    REQUIRE_THAT (b.getState().getParameter (chroma::ParamIDs::masterVolume)->getValue(),
                  Catch::Matchers::WithinAbs (a.getState().getParameter (chroma::ParamIDs::masterVolume)->getValue(), 1e-6));
}
