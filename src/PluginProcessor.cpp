#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamIDs.h"

namespace chroma
{
ChromaProcessor::ChromaProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "CHROMA5YNTH", createParameterLayout())
{
}

void ChromaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    setLatencySamples (synth.getLatencySamples());
}

bool ChromaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void ChromaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                bpm = *b;
    synth.process (buffer, midi, bpm);
}

juce::AudioProcessorEditor* ChromaProcessor::createEditor()
{
    return new ChromaEditor (*this);
}

void ChromaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = state.copyState();
    tree.removeChild (tree.getChildWithName ("CORE"), nullptr);
    tree.appendChild (synth.saveState(), nullptr);
    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void ChromaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            const auto core = tree.getChildWithName ("CORE");
            tree.removeChild (core, nullptr);
            state.replaceState (tree);
            synth.loadState (core);
        }
}
} // namespace chroma

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new chroma::ChromaProcessor();
}
