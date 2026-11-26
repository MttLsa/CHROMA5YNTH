#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

// =====================================================================================
//  TMFU audiovibe visual language (from DUALBOX): flat, minimal, dark device view.
//  Rules and exact values: STYLE.md. Never hard-code colours/fonts elsewhere: use chroma::style.
// =====================================================================================
namespace chroma::style
{
const juce::Colour bg      { 0xff1e1e1e };   // window background
const juce::Colour panel   { 0xff2a2a2a };   // panel body
const juce::Colour panel2  { 0xff333333 };   // buttons, combos, knob disc, segment off
const juce::Colour header  { 0xff3a3a3a };   // panel header, tooltip, title bars
const juce::Colour outline { 0xff121212 };
const juce::Colour track   { 0xff4a4a4a };   // knob / slider track
const juce::Colour text    { 0xffd0d0d0 };
const juce::Colour textDim { 0xff8c8c8c };
const juce::Colour accent  { 0xff4f8cff };   // primary accent - blue
const juce::Colour accent2 { 0xffb4dc3c };   // secondary accent - lime
const juce::Colour accentG { 0xffeeeeee };   // global / output - white

// Inter (SIL OFL 1.1): Regular for text, SemiBold for titles/buttons/bold. Embedded from assets/fonts.
juce::Typeface::Ptr uiTypeface (bool bold);

inline juce::Font font (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (uiTypeface (bold)).withHeight (h));
}

// Sizes (px, at 100 % scale)
inline constexpr int margin = 8;
inline constexpr int titleH = 56;        // header bar with the wordmark
inline constexpr int panelHeaderH = 20;
inline constexpr float panelRadius = 4.0f;
inline constexpr float controlRadius = 2.5f;
} // namespace chroma::style

namespace chroma
{
// UTF-8 C string (source literals, generated tables) -> juce::String. juce::String (const char*) assumes ASCII.
inline juce::String utf8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

// Generic parameter binding used by the final GUI: host parameters (APVTS) or internal layer
// parameters (LayerParamStore). Values are plain (not normalised). get() must be lock-free.
struct Binding
{
    enum class Kind { Float, Int, Bool, Choice };
    juce::String name, unit, tooltip;
    Kind kind = Kind::Float;
    float min = 0.0f, max = 1.0f, def = 0.0f, skewCentre = 0.0f;
    bool hasSkew = false;
    juce::StringArray choices;                 // Choice labels (Bool: Off/On)
    juce::StringArray groups;                  // optional sub-menu per choice (long lists)
    juce::NormalisableRange<float> hostRange;  // used when hasHostRange (host parameters)
    bool hasHostRange = false;
    std::function<float()> get;
    std::function<void (float)> set;
    std::function<void()> beginGesture, endGesture;
    juce::String format (float v) const;       // value + unit for readouts
    juce::NormalisableRange<double> range() const;
};
using BindingPtr = std::shared_ptr<Binding>;

class StyleLookAndFeel : public juce::LookAndFeel_V4
{
public:
    StyleLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override { return style::font (13.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return style::font (juce::jmin (12.5f, (float) h * 0.6f)); }
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override { return style::uiTypeface (f.isBold()); }
    void drawDocumentWindowTitleBar (juce::DocumentWindow&, juce::Graphics&, int w, int h, int, int, const juce::Image*, bool) override;
};

// Rotary slider: 700 px for the full range (Shift = 4x finer); arrow keys step when focused.
class FineSlider : public juce::Slider
{
public:
    FineSlider();
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaintParent(); }
    void focusLost (FocusChangeType) override { repaintParent(); }
    int dragPixels = 700;

private:
    void repaintParent() { if (auto* p = getParentComponent()) p->repaint(); }
};

// Knob: caption on top, rotary in the middle, value readout below.
class Knob : public juce::Component
{
public:
    explicit Knob (const juce::String& caption = {}, juce::Colour accent = style::accent);
    void attach (juce::AudioProcessorValueTreeState&, const juce::String& paramId);
    void bind (BindingPtr);
    void refresh();                                // pulls the bound value (GUI timer)
    void setAccent (juce::Colour);
    void resized() override;
    void paint (juce::Graphics&) override;
    FineSlider slider;

private:
    juce::String caption;
    juce::Colour accent;
    juce::RangedAudioParameter* param = nullptr;
    BindingPtr binding;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

// Segmented selector for choice / bool / int parameters (e.g. LP | BP | HP).
class Segmented : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Segmented (juce::Colour accentColour = style::accent) : accent (accentColour) {}
    void attach (juce::AudioProcessorValueTreeState&, const juce::String& paramId, juce::StringArray labels = {});
    void bind (BindingPtr);
    void refresh();
    void setCaption (const juce::String& c) { caption = c; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> area() const;
    juce::Colour accent;
    juce::String caption;
    juce::StringArray labels;
    int index = 0;
    BindingPtr binding;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> att;
};

// Titled panel: header strip with accent tick and upper-case SemiBold title.
class Panel : public juce::Component
{
public:
    Panel (const juce::String& title, juce::Colour accent = style::accent);
    void paint (juce::Graphics&) override;
    juce::Rectangle<int> content() const { return getLocalBounds().withTrimmedTop (style::panelHeaderH).reduced (6, 4); }

private:
    juce::String title;
    juce::Colour accent;
};

// Header bar: "CHROMA" + dimmed "5YNTH" wordmark, maker line, subtitle, version.
void drawHeader (juce::Graphics&, juce::Rectangle<float> bar, const juce::String& subtitle, const juce::String& version);
} // namespace chroma
