#include "StyleLookAndFeel.h"

#include <BinaryData.h>

using namespace juce;

namespace chroma
{
Typeface::Ptr style::uiTypeface (bool bold)
{
    static const Typeface::Ptr regular = Typeface::createSystemTypefaceFor (ChromaAssets::InterRegular_ttf, ChromaAssets::InterRegular_ttfSize);
    static const Typeface::Ptr semi    = Typeface::createSystemTypefaceFor (ChromaAssets::InterSemiBold_ttf, ChromaAssets::InterSemiBold_ttfSize);
    return bold ? semi : regular;
}

StyleLookAndFeel::StyleLookAndFeel()
{
    using namespace style;
    setColour (ResizableWindow::backgroundColourId, bg);
    setColour (DocumentWindow::backgroundColourId, bg);
    setColour (Label::textColourId, text);
    setColour (TextButton::buttonColourId, panel2);
    setColour (TextButton::buttonOnColourId, accent);
    setColour (TextButton::textColourOffId, text);
    setColour (TextButton::textColourOnId, Colours::black);
    setColour (ToggleButton::textColourId, text);
    setColour (ToggleButton::tickColourId, accent);
    setColour (ComboBox::backgroundColourId, panel2);
    setColour (ComboBox::textColourId, text);
    setColour (ComboBox::outlineColourId, outline);
    setColour (ComboBox::arrowColourId, textDim);
    setColour (PopupMenu::backgroundColourId, panel2);
    setColour (PopupMenu::textColourId, text);
    setColour (PopupMenu::highlightedBackgroundColourId, accent);
    setColour (PopupMenu::highlightedTextColourId, Colours::black);
    setColour (Slider::rotarySliderFillColourId, accent);
    setColour (Slider::trackColourId, accent);
    setColour (Slider::textBoxTextColourId, text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (TooltipWindow::backgroundColourId, header);
    setColour (TooltipWindow::textColourId, text);
    setColour (TextEditor::backgroundColourId, panel2);
    setColour (TextEditor::textColourId, text);
    setColour (TextEditor::highlightColourId, accent.withAlpha (0.4f));
    setColour (TextEditor::outlineColourId, outline);
    setColour (CaretComponent::caretColourId, text);
    setColour (AlertWindow::backgroundColourId, panel);
    setColour (AlertWindow::textColourId, text);
    setColour (AlertWindow::outlineColourId, outline);
    setDefaultSansSerifTypeface (uiTypeface (false));
}

void StyleLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const auto bounds = Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float lineW = jmax (2.0f, radius * 0.16f);
    const float arcR = radius - lineW * 0.5f;
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;

    Path bgArc;
    bgArc.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (style::track);
    g.strokePath (bgArc, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::butt));

    const float angle = start + pos * (end - start);
    const float from = bipolar ? (start + end) * 0.5f : start;
    Path valArc;
    valArc.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, jmin (from, angle), jmax (from, angle), true);
    g.setColour (s.isEnabled() ? s.findColour (Slider::rotarySliderFillColourId) : style::textDim);
    g.strokePath (valArc, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::butt));

    const float innerR = arcR - lineW * 1.2f;
    g.setColour (style::panel2.brighter (0.05f));
    g.fillEllipse (c.x - innerR, c.y - innerR, innerR * 2.0f, innerR * 2.0f);
    Path ptr;
    ptr.startNewSubPath (c.getPointOnCircumference (innerR * 0.25f, angle));
    ptr.lineTo (c.getPointOnCircumference (innerR * 0.95f, angle));
    g.setColour (style::text);
    g.strokePath (ptr, PathStrokeType (jmax (1.5f, lineW * 0.6f), PathStrokeType::curved, PathStrokeType::rounded));
}

void StyleLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                         Slider::SliderStyle st, Slider& s)
{
    const auto r = Rectangle<int> (x, y, w, h).toFloat();
    const auto accent = s.findColour (Slider::trackColourId);
    const float zero = (float) s.valueToProportionOfLength (s.getMinimum() < 0.0 ? 0.0 : s.getMinimum());
    if (st == Slider::LinearHorizontal)
    {
        const auto tr = r.withSizeKeepingCentre (r.getWidth(), 4.0f);
        g.setColour (style::track);
        g.fillRoundedRectangle (tr, 2.0f);
        const float x0 = r.getX() + jlimit (0.0f, 1.0f, zero) * r.getWidth();
        g.setColour (accent);
        g.fillRoundedRectangle (Rectangle<float> (jmin (x0, pos), tr.getY(), std::abs (pos - x0), 4.0f), 2.0f);
        g.setColour (style::text);
        g.fillRoundedRectangle (Rectangle<float> (pos - 3.0f, r.getCentreY() - 8.0f, 6.0f, 16.0f), 2.0f);
    }
    else
    {
        const auto tr = r.withSizeKeepingCentre (4.0f, r.getHeight());
        g.setColour (style::track);
        g.fillRoundedRectangle (tr, 2.0f);
        g.setColour (accent);
        g.fillRoundedRectangle (Rectangle<float> (tr.getX(), pos, 4.0f, r.getBottom() - pos), 2.0f);
        g.setColour (style::text);
        g.fillRoundedRectangle (Rectangle<float> (r.getCentreX() - 8.0f, pos - 3.0f, 16.0f, 6.0f), 2.0f);
    }
}

void StyleLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour& bgc, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    Colour c = b.getToggleState() ? b.findColour (TextButton::buttonOnColourId) : bgc;
    if (down)
        c = c.brighter (0.15f);
    else if (over)
        c = c.brighter (0.07f);
    g.setColour (c);
    g.fillRoundedRectangle (r, style::controlRadius);
    g.setColour (style::outline);
    g.drawRoundedRectangle (r, style::controlRadius, 1.0f);
}

void StyleLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.findColour (b.getToggleState() ? TextButton::textColourOnId : TextButton::textColourOffId)
                     .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (2, 1), Justification::centred, 1);
}

void StyleLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool over, bool)
{
    const float sz = jmin (14.0f, (float) b.getHeight() - 4.0f);
    const auto box = Rectangle<float> (2.0f, ((float) b.getHeight() - sz) * 0.5f, sz, sz);
    g.setColour (b.getToggleState() ? b.findColour (ToggleButton::tickColourId) : style::panel2.brighter (over ? 0.07f : 0.0f));
    g.fillRoundedRectangle (box, 2.0f);
    g.setColour (style::outline);
    g.drawRoundedRectangle (box, 2.0f, 1.0f);
    g.setColour (style::text.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.setFont (style::font (12.0f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft ((int) sz + 8), Justification::centredLeft, 1);
}

void StyleLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    const auto r = Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    g.setColour (box.findColour (ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, style::controlRadius);
    g.setColour (style::outline);
    g.drawRoundedRectangle (r, style::controlRadius, 1.0f);
    Path p;
    const float ax = (float) w - 8.0f, ay = (float) h * 0.5f;
    p.addTriangle (ax - 3.0f, ay - 1.5f, ax + 3.0f, ay - 1.5f, ax, ay + 2.5f);
    g.setColour (style::textDim);
    g.fillPath (p);
}

Font StyleLookAndFeel::getComboBoxFont (ComboBox& b)
{
    return style::font (jlimit (10.0f, 12.0f, (float) b.getHeight() * 0.6f));
}

void StyleLookAndFeel::positionComboBoxText (ComboBox& b, Label& label)
{
    label.setBounds (2, 1, jmax (8, b.getWidth() - 16), b.getHeight() - 2);
    label.setFont (getComboBoxFont (b));
    label.setBorderSize ({ 0, 3, 0, 0 });
}

Label* StyleLookAndFeel::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (style::font (11.0f));
    l->setColour (Label::textColourId, style::text);
    l->setColour (Label::backgroundColourId, Colours::transparentBlack);
    l->setColour (Label::outlineColourId, Colours::transparentBlack);
    return l;
}

void StyleLookAndFeel::drawDocumentWindowTitleBar (DocumentWindow& win, Graphics& g, int w, int h, int, int, const Image*, bool)
{
    g.fillAll (style::header);
    g.setColour (style::text);
    g.setFont (style::font (13.0f, true));
    g.drawText (win.getName(), 10, 0, w - 80, h, Justification::centredLeft);
}

//==============================================================================
FineSlider::FineSlider()
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
}

void FineSlider::mouseDown (const MouseEvent& e)
{
    setMouseDragSensitivity (e.mods.isShiftDown() ? dragPixels * 4 : dragPixels);
    Slider::mouseDown (e);
}

bool FineSlider::keyPressed (const KeyPress& k)
{
    int dir = 0;
    if (k.isKeyCode (KeyPress::upKey) || k.isKeyCode (KeyPress::rightKey)) dir = 1;
    if (k.isKeyCode (KeyPress::downKey) || k.isKeyCode (KeyPress::leftKey)) dir = -1;
    if (dir == 0)
        return Slider::keyPressed (k);
    const double interval = getInterval();
    double v;
    if (interval > 0.0 && (getMaximum() - getMinimum()) / interval <= 400.0)
        v = getValue() + dir * interval;
    else
        v = proportionOfLengthToValue (jlimit (0.0, 1.0, valueToProportionOfLength (getValue())
                                                             + dir * (k.getModifiers().isShiftDown() ? 0.0002 : 0.002)));
    setValue (jlimit (getMinimum(), getMaximum(), v), sendNotificationSync);
    return true;
}

//==============================================================================
Knob::Knob (const String& c, Colour a) : caption (c), accent (a)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setColour (Slider::rotarySliderFillColourId, accent);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
    slider.setVelocityBasedMode (false);
    slider.setMouseDragSensitivity (slider.dragPixels);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

void Knob::setAccent (Colour c)
{
    accent = c;
    slider.setColour (Slider::rotarySliderFillColourId, c);
    repaint();
}

void Knob::attach (AudioProcessorValueTreeState& s, const String& id)
{
    att.reset();
    param = s.getParameter (id);
    if (param == nullptr)
        return;
    att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
    slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    slider.setTooltip (param->getName (64));
    if (caption.isEmpty())
        caption = param->getName (32);
    repaint();
}

static int captionHeight (int h) { return jlimit (10, 16, h / 6); }

void Knob::resized()
{
    auto r = getLocalBounds();
    const int capH = captionHeight (getHeight());
    r.removeFromTop (capH);
    r.removeFromBottom (capH);
    const int d = jmin (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (d, d));
}

void Knob::paint (Graphics& g)
{
    const int capH = captionHeight (getHeight());
    g.setFont (style::font (jlimit (9.5f, 14.0f, (float) capH * 0.85f)));
    g.setColour (style::textDim);
    g.drawFittedText (caption, 0, 0, getWidth(), capH, Justification::centred, 1, 0.7f);
    if (param != nullptr)
    {
        g.setColour (slider.isMouseOverOrDragging() || slider.hasKeyboardFocus (false) ? accent : style::text);
        g.drawFittedText (param->getCurrentValueAsText(), 0, getHeight() - capH, getWidth(), capH, Justification::centred, 1, 0.7f);
    }
    if (slider.hasKeyboardFocus (false))
    {
        g.setColour (accent.withAlpha (0.7f));
        g.drawRoundedRectangle (slider.getBounds().toFloat().expanded (2.0f), 4.0f, 1.2f);
    }
}

//==============================================================================
void Segmented::attach (AudioProcessorValueTreeState& s, const String& id, StringArray lab)
{
    att.reset();
    param = s.getParameter (id);
    if (param == nullptr)
        return;
    labels = lab;
    if (labels.isEmpty())
    {
        if (auto* c = dynamic_cast<AudioParameterChoice*> (param))
            labels = c->choices;
        else if (dynamic_cast<AudioParameterBool*> (param) != nullptr)
            labels = { "Off", "On" };
        else
            for (int i = (int) param->getNormalisableRange().start; i <= (int) param->getNormalisableRange().end; ++i)
                labels.add (String (i));
    }
    att = std::make_unique<ParameterAttachment> (*param, [this] (float v)
    {
        index = roundToInt (v - param->getNormalisableRange().start);
        repaint();
    });
    att->sendInitialUpdate();
    setTooltip (param->getName (64));
    if (caption.isEmpty())
        caption = param->getName (32);
}

Rectangle<int> Segmented::area() const
{
    auto r = getLocalBounds();
    if (caption.isNotEmpty())
        r.removeFromTop (14);
    return r;
}

void Segmented::paint (Graphics& g)
{
    if (caption.isNotEmpty())
    {
        g.setColour (style::textDim);
        g.setFont (style::font (11.0f));
        g.drawText (caption, getLocalBounds().removeFromTop (14), Justification::centred);
    }
    const auto r = area().toFloat();
    const int n = jmax (1, labels.size());
    g.setColour (style::outline);
    g.fillRoundedRectangle (r, 3.0f);
    for (int i = 0; i < n; ++i)
    {
        const auto cell = Rectangle<float> (r.getX() + r.getWidth() * (float) i / (float) n, r.getY(),
                                            r.getWidth() / (float) n, r.getHeight()).reduced (0.75f);
        const bool on = i == index;
        g.setColour (on ? accent : style::panel2);
        g.fillRoundedRectangle (cell, 2.0f);
        g.setColour (on ? Colours::black : style::text);
        g.setFont (style::font (jlimit (9.5f, 13.0f, cell.getHeight() * 0.55f), on));
        g.drawFittedText (labels[i], cell.toNearestInt().reduced (1, 0), Justification::centred, 1, 0.6f);
    }
}

void Segmented::mouseDown (const MouseEvent& e)
{
    if (att == nullptr || labels.isEmpty())
        return;
    const auto r = area();
    const int n = labels.size();
    const int i = jlimit (0, n - 1, (e.x - r.getX()) * n / jmax (1, r.getWidth()));
    att->setValueAsCompleteGesture (param->getNormalisableRange().start + (float) i);
}

//==============================================================================
Panel::Panel (const String& t, Colour a) : title (t), accent (a) {}

void Panel::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (style::panel);
    g.fillRoundedRectangle (r, style::panelRadius);
    auto h = r.removeFromTop ((float) style::panelHeaderH);
    g.setColour (style::header);
    g.fillRoundedRectangle (h, style::panelRadius);
    g.fillRect (h.withTrimmedTop (h.getHeight() * 0.5f));
    g.setColour (accent);
    g.fillRect (Rectangle<float> (h.getX(), h.getY() + 4.0f, 3.0f, h.getHeight() - 8.0f));
    g.setColour (style::text);
    g.setFont (style::font (12.0f, true));
    g.drawFittedText (title.toUpperCase(), h.withTrimmedLeft (10).withTrimmedRight (10).toNearestInt(), Justification::centredLeft, 1, 0.8f);
}

//==============================================================================
void drawHeader (Graphics& g, Rectangle<float> bar, const String& subtitle, const String& version)
{
    const auto f = style::font (26.0f, true).withExtraKerningFactor (0.18f);
    const float base = bar.getCentreY() + 4.0f;

    GlyphArrangement ga;
    ga.addLineOfText (f, "CHROMA", bar.getX() + 4.0f, base);
    const float w1 = ga.getBoundingBox (0, -1, true).getWidth();
    g.setColour (style::text);
    ga.draw (g);

    GlyphArrangement gb;
    gb.addLineOfText (f, "5YNTH", bar.getX() + 4.0f + w1 + 4.0f, base);
    const float w2 = gb.getBoundingBox (0, -1, true).getWidth();
    g.setColour (style::text.withAlpha (0.55f));
    gb.draw (g);

    GlyphArrangement gm;
    gm.addLineOfText (style::font (9.5f).withExtraKerningFactor (0.12f), "TMFU audiovibe", bar.getX() + 5.0f, base + 12.0f);
    g.setColour (style::text.withAlpha (0.85f));
    gm.draw (g);

    const float x = bar.getX() + 4.0f + w1 + 4.0f + w2 + 14.0f;
    const float midY = base - 9.0f;
    g.setColour (style::accent);
    g.fillRoundedRectangle (x, midY - 8.0f, 6.0f, 16.0f, 1.5f);
    g.setColour (style::accent2);
    g.fillRoundedRectangle (x + 9.0f, midY - 8.0f, 6.0f, 16.0f, 1.5f);

    g.setColour (style::textDim);
    g.setFont (style::font (12.0f));
    g.drawText (subtitle, (int) (x + 28.0f), (int) (midY - 10.0f), 400, 20, Justification::centredLeft);
    g.setColour (style::textDim.withAlpha (0.6f));
    g.drawText (version, bar.toNearestInt().withTrimmedRight (6), Justification::centredRight);
}
} // namespace chroma
