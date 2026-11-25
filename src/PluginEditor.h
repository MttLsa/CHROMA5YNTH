#pragma once

#include "PluginProcessor.h"
#include "gui/StyleLookAndFeel.h"
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
    StyleLookAndFeel lookAndFeel;      // declared first: destroyed after every child
    std::vector<std::unique_ptr<Panel>> panels;
    std::vector<std::unique_ptr<Control>> controls;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
    juce::Viewport viewport;
    juce::Component content;
    int layoutContent (int width);
    juce::TextButton aboutButton { "About" };
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChromaEditor)
};
} // namespace chroma
