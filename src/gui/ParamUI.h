#pragma once

#include "StyleLookAndFeel.h"
#include "../PluginProcessor.h"
#include "LayerParams.h"

// =====================================================================================
//  Final GUI building blocks (§7): parameter bindings for host and layer parameters,
//  generic widgets, section panels laid out in a masonry page, display helpers.
//  All layer bindings follow UiContext::layer (the selected layer) dynamically.
// =====================================================================================
namespace chroma::ui
{
inline constexpr int cellH = 78, knobW = 60, comboW = 128;
inline constexpr std::array<const char*, 5> engineNames { "Analog", "Additive", "FM6", "Wavetable", "PD" };

class UiContext
{
public:
    explicit UiContext (ChromaProcessor& p) : proc (p) {}
    ChromaProcessor& proc;
    int layer = 0;                                      // selected layer 0..11

    Synth& synth() { return proc.getSynth(); }
    LayerParamStore& store() { return proc.getSynth().layerStore (layer); }
    juce::RangedAudioParameter* hostParam (const juce::String& id) { return proc.getState().getParameter (id); }
    juce::RangedAudioParameter* layerHostParam (int l, const juce::String& suffix)
    {
        return hostParam ("layer" + juce::String (l + 1) + "_" + suffix);
    }

    BindingPtr host (const juce::String& id);                          // fixed host parameter
    BindingPtr hostOfLayer (const juce::String& suffix);               // layerN_<suffix> of the selected layer
    BindingPtr layerParam (int lpId);                                  // internal parameter of the selected layer

    // Right click menu (Italian): default, MIDI learn, macro, host automation slot.
    void showMenu (const juce::String& hostId, int lpId, BindingPtr, juce::Component& target);

    std::function<void()> openWavetableEditor;
};

// One parameter control: Knob (float/int), Segmented (bool, choice ≤ 5) or captioned combo.
class ParamWidget : public juce::Component
{
public:
    ParamWidget (UiContext&, BindingPtr, juce::String hostId, int lpId, const juce::String& caption,
                 juce::Colour accent = style::accent, bool compact = false);   // compact: bar slider / combo, no caption
    ~ParamWidget() override;
    int preferredWidth() const noexcept { return prefW; }
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    struct Combo;
    struct Bar;
    UiContext& ctx;
    BindingPtr binding;
    juce::String hostId, caption;
    int lpId = -1, prefW = knobW;
    std::unique_ptr<Knob> knob;
    std::unique_ptr<Segmented> seg;
    std::unique_ptr<Combo> combo;
    std::unique_ptr<Bar> bar;
};

// Small power toggle (panel headers, layer tiles).
class PowerButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PowerButton (BindingPtr b, juce::String label = {}) : binding (std::move (b)), text (std::move (label)) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh();
    bool isOn() const noexcept { return on; }
    juce::Colour onColour = style::accent;

private:
    BindingPtr binding;
    juce::String text;
    bool on = false;
};

// Masonry page block.
class Block : public juce::Component
{
public:
    virtual int heightFor (int width) = 0;
    virtual void refresh() {}
    bool wide = false;
};

// Titled panel holding the parameters of one section; optional header toggle and envelope/LFO display.
class SectionPanel : public Block
{
public:
    SectionPanel (UiContext&, const juce::String& title, juce::Colour accent = style::accent);
    void addLayerParam (int lpId, const juce::String& caption);
    void addHostParam (const juce::String& id, const juce::String& caption);
    void addHostOfLayer (const juce::String& suffix, const juce::String& caption);
    void addWidget (std::unique_ptr<ParamWidget>);
    void setHeaderToggle (BindingPtr);
    void setDisplay (std::unique_ptr<juce::Component>, int height);
    void addExtra (std::unique_ptr<juce::Component>, int width);     // e.g. buttons in the flow

    int heightFor (int width) override;
    void refresh() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int layoutCells (int width, bool apply);
    UiContext& ctx;
    juce::String title;
    juce::Colour accent;
    std::vector<std::unique_ptr<juce::Component>> cells;
    std::vector<int> cellW;
    std::unique_ptr<PowerButton> toggle;
    std::unique_ptr<juce::Component> display;
    int displayH = 0;
};

// Builds a SectionPanel from every layer parameter of a section (lp::descs order), stripping the
// common caption prefix; a leading "<x>_on" bool becomes the header toggle; adds envelope/LFO view.
std::unique_ptr<SectionPanel> makeLayerSection (UiContext&, const juce::String& section, const juce::String& title,
                                                juce::Colour accent = style::accent);
std::unique_ptr<SectionPanel> makeHostSection (UiContext&, const juce::String& section, const juce::String& title,
                                               juce::Colour accent = style::accent);

// 16-slot modulation matrix table.
class ModMatrixBlock : public Block
{
public:
    explicit ModMatrixBlock (UiContext&);
    int heightFor (int) override { return style::panelHeaderH + 22 + 16 * 26 + 8; }
    void refresh() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    UiContext& ctx;
    std::vector<std::unique_ptr<juce::Component>> row[16];
    std::vector<ParamWidget*> widgets;
    std::vector<PowerButton*> toggles;
};

// Scrollable masonry of blocks.
class Page : public juce::Component
{
public:
    Page();
    void add (std::unique_ptr<Block>);
    void clear();
    void refresh();
    void resized() override;

private:
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<Block>> blocks;
};
} // namespace chroma::ui
