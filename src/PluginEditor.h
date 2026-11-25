#pragma once

#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace chroma
{
// Generic editor built from the generated GuiLayout table (replaced by the final GUI in a later phase).
class ChromaEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ChromaEditor (ChromaProcessor&);
    ~ChromaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static juce::String aboutText();

private:
    struct Control;
    std::vector<std::unique_ptr<Control>> controls;
    juce::TextButton aboutButton { "About" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChromaEditor)
};
} // namespace chroma
