#pragma once

#include "PluginProcessor.h"
#include "gui/StyleLookAndFeel.h"
#include "gui/WavetableEditor.h"
#include "gui/PresetBrowser.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace chroma
{
// Generic editor built from the generated GuiLayout table (replaced by the final GUI in a later phase).
class ChromaEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ChromaEditor (ChromaProcessor&);
    ~ChromaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static juce::String aboutText();

private:
    struct Control;
    struct LearnMenu;
    ChromaProcessor& proc;
    StyleLookAndFeel lookAndFeel;      // declared first: destroyed after every child
    std::vector<std::unique_ptr<Panel>> panels;
    std::vector<std::unique_ptr<Control>> controls;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
    juce::Viewport viewport;
    juce::Component content;
    int layoutContent (int width);
    juce::TextButton aboutButton { "About" };
    juce::TextButton wtButton { "WT Editor" };
    std::unique_ptr<WavetableEditor> wtEditor;
    juce::TooltipWindow tooltips { this, 600 };

    // §5/§6 toolbar
    juce::TextButton presetsButton { "Presets" }, undoButton { "Undo" }, redoButton { "Redo" },
                     aButton { "A" }, bButton { "B" }, copyAbButton { "A>B" }, randomButton { "Randomize" },
                     initButton { "Init" }, tuningButton { "Tuning" }, midiButton { "MIDI" };
    juce::Slider randomAmount { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label presetName;
    std::unique_ptr<PresetBrowser> browser;
    std::unique_ptr<juce::FileChooser> chooser;
    void timerCallback() override;
    void showRandomMenu();
    void showTuningMenu();
    void showMidiMenu();
    void showLearnMenu (const juce::String& paramId, juce::Component& target);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChromaEditor)
};
} // namespace chroma
