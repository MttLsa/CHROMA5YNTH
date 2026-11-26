#pragma once

#include "ParamUI.h"

// =====================================================================================
//  Final GUI (§7): layer bar, master column (meter, macros, SCALE keyboard) and the
//  dedicated editors (additive custom spectrum, PD phase curve, FM6 bank import).
// =====================================================================================
namespace chroma::ui
{
// Peak meter with time-based decay (opaque: repainting it never repaints its parents).
class LevelMeter : public juce::Component
{
public:
    LevelMeter() { setOpaque (true); }
    void push (float peak, double dtSeconds);
    void paint (juce::Graphics&) override;

private:
    float level = 0.0f, hold = 0.0f;
    double holdAge = 0.0;
    int drawnPx = -1, drawnHoldPx = -1;
};

class StereoMeter : public juce::Component
{
public:
    StereoMeter();
    void update (Synth&, double dt);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    LevelMeter left, right;
    int voices = -1;
};

class LayerTile : public juce::Component, public juce::SettableTooltipClient
{
public:
    LayerTile (UiContext&, int layer);
    void refresh();
    void updateMeter (double dt);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void (int)> onSelect;
    std::function<void (int, juce::Component&)> onMenu;

private:
    juce::Rectangle<int> engineArea() const;
    UiContext& ctx;
    const int layer;
    PowerButton power, mute, solo;
    LevelMeter meter;
    BindingPtr engine;
    int engineIndex = -1, voices = -1;
    bool selected = false, shownOn = false;
};

// SCALE (§1.1): 12 keys, each a mask of layers; clicks edit the selected layer's bit.
class ScaleKeyboard : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ScaleKeyboard (UiContext&);
    void refresh();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> keyRect (int k) const;
    int keyAt (juce::Point<float>) const;
    UiContext& ctx;
    std::array<juce::RangedAudioParameter*, 12> slots {};
    std::array<int, 12> masks {};
    int shownLayer = -1;
};

// Additive custom spectrum A/B: bar editor for any per-partial field.
class AdditiveEditor : public Block
{
public:
    explicit AdditiveEditor (UiContext&);
    int heightFor (int) override { return 270; }
    void refresh() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

private:
    juce::Rectangle<float> bars() const;
    float toNorm (int field, float v) const;
    float fromNorm (int field, float n) const;
    void drawAt (juce::Point<float>, bool line);
    UiContext& ctx;
    juce::TextButton abA { "A" }, abB { "B" }, shapeButton { "Load Shape" };
    juce::ComboBox field, count;
    int ab = 0, hover = -1, lastK = -1, shownLayer = -1;
    float lastV = 0.0f;
    uint32_t shownVersion = 0;
};

// PD custom phase curve: drag points, double click adds/removes, right click removes.
class PdCurveEditor : public Block
{
public:
    explicit PdCurveEditor (UiContext&);
    int heightFor (int) override { return 250; }
    void refresh() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> plot() const;
    int pointAt (juce::Point<float>) const;
    void publish();
    UiContext& ctx;
    juce::TextButton interp[3] { juce::TextButton ("Linear"), juce::TextButton ("Smooth"), juce::TextButton ("Cubic") };
    juce::TextButton resetButton { "Reset" };
    std::vector<pd::PhaseCurve::Point> pts;
    int mode = 0, drag = -1, shownLayer = -1;
    uint32_t shownVersion = 0;
};

// Panel with action buttons (FM6 bank import, wavetable editor).
class ToolsBlock : public Block
{
public:
    ToolsBlock (const juce::String& title, juce::String info);
    void addButton (const juce::String& text, const juce::String& tip, std::function<void (juce::Component&)>);
    void setInfo (const juce::String& s) { info = s; repaint(); }
    int heightFor (int) override { return style::panelHeaderH + 66; }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String title, info;
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
};
} // namespace chroma::ui
