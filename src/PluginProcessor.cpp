#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamIDs.h"

namespace chroma
{
ChromaProcessor::ChromaProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "CHROMA5YNTH", createParameterLayout())
{
    masterVolumeDb = state.getRawParameterValue (ParamIDs::masterVolume);
}

void ChromaProcessor::prepareToPlay (double sampleRate, int)
{
    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (masterVolumeDb->load()));
}

bool ChromaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void ChromaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Engines/layers arrive in later phases; keep the smoother advancing.
    masterGain.setTargetValue (juce::Decibels::decibelsToGain (masterVolumeDb->load(), -60.0f));
    masterGain.skip (buffer.getNumSamples());
}

juce::AudioProcessorEditor* ChromaProcessor::createEditor()
{
    return new ChromaEditor (*this);
}

void ChromaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ChromaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}
} // namespace chroma

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new chroma::ChromaProcessor();
}
