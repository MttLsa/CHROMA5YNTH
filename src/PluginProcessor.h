#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "core/Synth.h"
#include "core/History.h"
#include "core/Randomizer.h"
#include "preset/PresetManager.h"

namespace chroma
{
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

class ChromaProcessor final : public juce::AudioProcessor,
                              private juce::AudioProcessorListener,
                              private juce::Timer
{
public:
    ChromaProcessor();
    ~ChromaProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return CHROMA_PRODUCT_NAME; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Init"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
    Synth& getSynth() noexcept { return synth; }

    // ---- §5/§6 state tools (message thread)
    static bool isGlobalParam (const juce::String& id);
    // Editable state; includeGlobal = false leaves out Global settings, MPE, MIDI map and microtuning.
    juce::ValueTree captureState (bool includeGlobal);
    void restoreState (const juce::ValueTree&, bool keepGlobal);
    void idle();                                     // MIDI learn, CC -> host, undo checkpoints (timer, 30 Hz)

    History& getHistory() noexcept { return history; }
    bool undo();
    bool redo();
    void commitHistory();                            // records pending edits as an undo step now
    void selectAB (int slot);
    int randomize (const RandomizeScope&, float amount, juce::int64 seed = 0);   // seed 0 = time based
    void initAll();                                  // whole patch back to defaults (Global kept)
    void initLayer (int layer);                      // Init button: only the selected synth (its On state is kept)

    PresetManager& getPresets();
    void setPresetRoot (const juce::File&);          // tests / custom library location
    juce::File saveMultiPreset (const PresetMeta&);
    bool loadMultiPreset (const juce::File&);
    juce::File saveLayerPreset (int layer, const PresetMeta&);
    bool loadLayerPreset (int layer, const juce::File&);
    const juce::String& getCurrentPresetName() const noexcept { return currentPresetName; }

    juce::Point<int> editorSize;                     // last editor size (saved with the host state)

private:
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { ++paramEdits; }
    void audioProcessorChanged (juce::AudioProcessor*, const juce::AudioProcessorListener::ChangeDetails&) override {}
    void timerCallback() override { idle(); }
    uint32_t editCounter() const noexcept { return paramEdits.load() + synth.editCount(); }
    void markClean();

    juce::AudioProcessorValueTreeState state;
    Synth synth { *this, state };
    History history;
    std::unique_ptr<PresetManager> presets;
    juce::File presetRoot;
    juce::String currentPresetName { "Init" };
    std::atomic<uint32_t> paramEdits { 0 };
    uint32_t lastSeenEdits = 0;
    double lastEditMs = 0.0;
    bool pendingCommit = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChromaProcessor)
};
} // namespace chroma
