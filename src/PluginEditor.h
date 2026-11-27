#pragma once

#include "PluginProcessor.h"
#include "gui/StyleLookAndFeel.h"
#include "gui/MainPanels.h"
#include "gui/WavetableEditor.h"
#include "gui/PresetBrowser.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace chroma
{
// Final editor (§7): header + toolbar, synth bar 1–12, engine/voice/mod/FX pages in the centre (modules in
// signal-flow order, engine filter last), Master + Macro + NOTE ASSIGN + GLOBAL SCALE on the right. Vector UI drawn at a logical 1280×800 base scaled 75–200 %;
// beyond that (up to the full screen) the layout grows. Meters/controls refresh on v-blank ≤ 60 fps,
// reading lock-free values only.
class ChromaEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ChromaEditor (ChromaProcessor&);
    ~ChromaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static juce::String aboutText();
    static constexpr int baseW = 1280, baseH = 800;
    static constexpr float minScale = 0.75f, maxScale = 2.0f;
    float currentScale() const noexcept { return scale; }
    juce::Rectangle<int> logicalBounds() const { return root.getBounds(); }
    void tick() { refreshControls(); }           // one control refresh pass (tests)
    void showTab (int t) { selectTab (t); }
    void showLayer (int l) { selectLayer (l); }
    int engineShown() const noexcept { return builtEngine; }

private:
    struct Root final : juce::Component
    {
        explicit Root (ChromaEditor& e) : ed (e) {}
        void paint (juce::Graphics& g) override { ed.paintRoot (g); }
        void resized() override { ed.layoutRoot(); }
        ChromaEditor& ed;
    };
    enum Tab { tabEngine, tabVoice, tabMod, tabFx, tabMasterFx, tabGlobal, numTabs };

    void paintRoot (juce::Graphics&);
    void layoutRoot();
    void buildStaticPages();
    void buildEnginePage (int engine);
    void selectLayer (int);
    void selectTab (int);
    void onFrame();
    void refreshControls();
    void updateToolbar();
    void showLayerMenu (int layer, juce::Component&);
    void showZoomMenu();
    void importFmBank (juce::Component&);
    void showRandomMenu();
    void showTuningMenu();
    void showMidiMenu();

    ChromaProcessor& proc;
    StyleLookAndFeel lookAndFeel;      // declared first: destroyed after every child
    ui::UiContext ctx { proc };
    Root root { *this };
    float scale = 1.0f;

    juce::TextButton aboutButton { "About" }, zoomButton { "100%" };
    juce::TextButton presetsButton { "Presets" }, undoButton { "Undo" }, redoButton { "Redo" },
                     aButton { "A" }, bButton { "B" }, copyAbButton { "A>B" }, randomButton { "Randomize" },
                     initButton { "Init" }, tuningButton { "Tuning" }, midiButton { "MIDI" };
    juce::Slider randomAmount { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label presetName;

    std::array<std::unique_ptr<ui::LayerTile>, numLayers> tiles;
    std::array<juce::TextButton, numTabs> tabs;
    std::array<ui::Page, numTabs> pages;
    int currentTab = tabEngine, builtEngine = -1, builtLayer = -1;
    ui::ToolsBlock* fmTools = nullptr;

    std::unique_ptr<ui::SectionPanel> masterPanel, macroPanel, scalePanel, gscalePanel;
    ui::StereoMeter* stereoMeter = nullptr;
    ui::ScaleKeyboard* keyboard = nullptr;

    std::unique_ptr<WavetableEditor> wtEditor;
    std::unique_ptr<PresetBrowser> browser;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ValueTree layerClipboard;
    juce::TooltipWindow tooltips { this, 600 };

    double lastFrameMs = 0.0;
    int frameCount = 0;
    juce::VBlankAttachment vblank { this, [this] { onFrame(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChromaEditor)
};
} // namespace chroma
