#pragma once
#include "PluginProcessor.h"
#include "ParamIDs.h"

namespace test
{
inline void setParam (chroma::ChromaProcessor& p, const char* id, float plain)
{
    auto* prm = p.getState().getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

struct Rig
{
    chroma::ChromaProcessor proc;
    juce::AudioBuffer<float> buffer { 2, 64 };
    Rig()
    {
        proc.setPlayConfigDetails (0, 2, 48000.0, 64);
        proc.prepareToPlay (48000.0, 64);
    }
    void run (juce::MidiBuffer midi = {}, int blocks = 1)
    {
        for (int b = 0; b < blocks; ++b)
        {
            proc.processBlock (buffer, midi);
            midi.clear();
        }
    }
    void noteOn (int note, float vel = 0.8f, int ch = 1)
    {
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (ch, note, vel), 0);
        run (m);
    }
    void noteOff (int note, int ch = 1)
    {
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOff (ch, note), 0);
        run (m);
    }
    chroma::Synth& synth() { return proc.getSynth(); }
    // Layers sounding `note` (bitmask), from active voices.
    int layersPlaying (int note)
    {
        int mask = 0;
        for (int i = 0; i < chroma::maxPoolVoices; ++i)
        {
            const auto v = synth().voiceInfo (i);
            if (v.active && v.note == note) mask |= 1 << v.layer;
        }
        return mask;
    }
    bool notePlaying (int note) { return layersPlaying (note) != 0; }
};
} // namespace test
