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
    setColour (TextButton::buttonOnColourId, accentG);    // toolbar selections: white (tabs and layer tiles set blue)
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
    {
        const auto ch = k.getTextCharacter();
        if (onTypedChar != nullptr && ((ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == ',' || k.isKeyCode (KeyPress::returnKey))
            && onTypedChar (ch))
            return true;
        return Slider::keyPressed (k);
    }
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
String Binding::format (float v) const
{
    if (kind == Kind::Choice || kind == Kind::Bool)
        return choices[jlimit (0, jmax (0, choices.size() - 1), roundToInt (v - min))];
    if (kind == Kind::Int)
        return String (roundToInt (v)) + (unit.isNotEmpty() ? " " + unit : String());
    auto num = [] (float x)
    {
        const float a = std::abs (x);
        return String (x, a < 10.0f ? 2 : a < 100.0f ? 1 : 0);
    };
    if (unit == "%" && min >= -1.0f && max <= 2.0f)
        return String (roundToInt (v * 100.0f)) + "%";
    if (unit == "s" && std::abs (v) < 1.0f)
        return String (roundToInt (v * 1000.0f)) + " ms";
    if (unit == "ms" && std::abs (v) >= 1000.0f)
        return num (v * 0.001f) + " s";
    if (unit == "Hz" && std::abs (v) >= 1000.0f)
        return num (v * 0.001f) + " kHz";
    return num (v) + (unit.isNotEmpty() ? (unit == "%" || unit == utf8 ("°") ? unit : " " + unit) : String());
}

std::optional<float> Binding::parse (const String& input) const
{
    auto t = input.trim().replaceCharacter (',', '.');
    if (t.isEmpty()) return std::nullopt;
    if (kind == Kind::Choice || kind == Kind::Bool)
    {
        for (int i = 0; i < choices.size(); ++i)
            if (choices[i].equalsIgnoreCase (t)) return min + (float) i;
        if (kind == Kind::Bool)
        {
            if (t.equalsIgnoreCase ("on") || t == "1") return max;
            if (t.equalsIgnoreCase ("off") || t == "0") return min;
        }
        if (t.containsOnly ("0123456789"))
        {
            const int i = t.getIntValue() - 1;    // 1-based position in the list
            if (i >= 0 && i < choices.size()) return min + (float) i;
        }
        return std::nullopt;
    }
    const auto lower = t.toLowerCase().removeCharacters (" ");
    const auto numText = lower.initialSectionContainingOnly ("+-0123456789.e");
    if (numText.isEmpty() || ! numText.containsAnyOf ("0123456789")) return std::nullopt;
    float v = numText.getFloatValue();
    const auto suffix = lower.substring (numText.length());
    const auto shown = format (get != nullptr ? get() : def);        // the readout tells the unit the user sees
    if (suffix.startsWith ("k")) v *= 1000.0f;                        // 1.2k, 1.2khz
    else if (suffix == "ms") { if (unit == "s") v *= 0.001f; }
    else if (suffix == "s" && unit == "ms") v *= 1000.0f;
    else if (suffix == "%" || (suffix.isEmpty() && unit == "%" && min >= -1.0f && max <= 2.0f))
    {
        if (unit == "%" && min >= -1.0f && max <= 2.0f) v *= 0.01f;
    }
    else if (suffix.isEmpty() && unit == "s" && shown.endsWith (" ms")) v *= 0.001f;
    else if (suffix.isEmpty() && unit == "Hz" && shown.endsWith ("kHz")) v *= 1000.0f;
    if (kind == Kind::Int) v = (float) roundToInt (v);
    return jlimit (min, max, v);
}

NormalisableRange<double> Binding::range() const
{
    if (hasHostRange)
    {
        const auto& h = hostRange;
        return { (double) h.start, (double) h.end,
                 [h] (double s, double e, double x) { auto c = h; c.start = (float) s; c.end = (float) e; return (double) c.convertFrom0to1 ((float) x); },
                 [h] (double s, double e, double v) { auto c = h; c.start = (float) s; c.end = (float) e; return (double) c.convertTo0to1 ((float) v); },
                 [h] (double, double, double v) { return (double) h.snapToLegalValue ((float) v); } };
    }
    NormalisableRange<double> r (min, jmax (min + 1.0e-6f, max), kind == Kind::Float ? 0.0 : 1.0);
    if (hasSkew && skewCentre > min && skewCentre < max)
        r.setSkewForCentre (skewCentre);
    return r;
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
    slider.onTypedChar = [this] (juce_wchar ch)
    {
        if (binding == nullptr) return false;
        beginTextEntry (ch == '\r' || ch == '\n' || ch == 0 ? String() : String::charToString (ch));
        return true;
    };
    addAndMakeVisible (slider);
}

void Knob::beginTextEntry (const String& initial)
{
    if (binding == nullptr) return;
    editor = std::make_unique<TextEditor>();
    editor->setFont (style::font (11.5f));
    editor->setJustification (Justification::centred);
    editor->setColour (TextEditor::backgroundColourId, style::outline);
    editor->setColour (TextEditor::focusedOutlineColourId, accent);
    editor->setColour (TextEditor::outlineColourId, accent);
    editor->setColour (TextEditor::textColourId, style::text);
    editor->setIndents (2, 1);
    editor->setTooltip ("Scrivi un valore (es. 250 ms, 1.2k, 45%) e premi Invio; Esc annulla.");
    const int capH = jlimit (10, 16, getHeight() / 6);
    editor->setBounds (getLocalBounds().removeFromBottom (capH + 4).reduced (1, 0));
    editor->setText (initial.isNotEmpty() ? initial : binding->format ((float) slider.getValue()), false);
    editor->onReturnKey = [this] { endTextEntry (true); };
    editor->onEscapeKey = [this] { endTextEntry (false); };
    editor->onFocusLost = [this] { endTextEntry (true); };
    addAndMakeVisible (*editor);
    editor->grabKeyboardFocus();
    if (initial.isNotEmpty()) editor->moveCaretToEnd();
    else editor->selectAll();
}

void Knob::endTextEntry (bool apply)
{
    if (editor == nullptr) return;
    const auto text = editor->getText();
    auto keep = std::move (editor);          // destroyed after the callbacks returned
    keep->setVisible (false);
    MessageManager::callAsync ([k = std::shared_ptr<TextEditor> (std::move (keep))] {});
    if (apply && binding != nullptr)
        if (const auto v = binding->parse (text))
        {
            if (binding->beginGesture) binding->beginGesture();
            slider.setValue (*v, sendNotificationSync);
            if (binding->endGesture) binding->endGesture();
        }
    repaint();
}

void Knob::mouseDoubleClick (const MouseEvent& e)
{
    // double click on the value readout = type a value (double click on the knob itself = default)
    if (e.y >= getHeight() - jlimit (10, 16, getHeight() / 6) - 2)
        beginTextEntry ({});
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

void Knob::bind (BindingPtr b)
{
    att.reset();
    param = nullptr;
    binding = std::move (b);
    slider.setNormalisableRange (binding->range());
    slider.setDoubleClickReturnValue (true, binding->def);
    slider.setValue (binding->get(), dontSendNotification);
    slider.setTooltip (binding->tooltip.isNotEmpty() ? binding->tooltip : binding->name);
    slider.onDragStart = [this] { if (binding->beginGesture) binding->beginGesture(); };
    slider.onDragEnd = [this] { if (binding->endGesture) binding->endGesture(); };
    slider.onValueChange = [this]
    {
        if (binding->set) binding->set ((float) slider.getValue());
        repaint();
    };
    if (caption.isEmpty())
        caption = binding->name;
    repaint();
}

void Knob::refresh()
{
    if (binding == nullptr || slider.isMouseButtonDown())
        return;
    const double v = binding->get();
    if (std::abs (v - slider.getValue()) > 1.0e-7)
    {
        slider.setValue (v, dontSendNotification);
        repaint();
    }
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
    if (param != nullptr || binding != nullptr)
    {
        g.setColour (slider.isMouseOverOrDragging() || slider.hasKeyboardFocus (false) ? accent : style::text);
        g.drawFittedText (binding != nullptr ? binding->format ((float) slider.getValue()) : param->getCurrentValueAsText(), 0, getHeight() - capH, getWidth(), capH, Justification::centred, 1, 0.7f);
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

void Segmented::bind (BindingPtr b)
{
    att.reset();
    param = nullptr;
    binding = std::move (b);
    labels = binding->choices;
    if (labels.isEmpty())
        for (int i = (int) binding->min; i <= (int) binding->max; ++i)
            labels.add (String (i));
    setTooltip (binding->tooltip.isNotEmpty() ? binding->tooltip : binding->name);
    index = -1;
    refresh();
}

void Segmented::refresh()
{
    if (binding == nullptr)
        return;
    const int i = roundToInt (binding->get() - binding->min);
    if (i != index)
    {
        index = i;
        repaint();
    }
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
    if ((att == nullptr && binding == nullptr) || labels.isEmpty() || e.mods.isPopupMenu())
        return;
    const auto r = area();
    const int n = labels.size();
    int i = jlimit (0, n - 1, (e.x - r.getX()) * n / jmax (1, r.getWidth()));
    if (binding != nullptr)
    {
        if (n == 2 && binding->kind == Binding::Kind::Bool && r.getWidth() < 48)
            i = index == 0 ? 1 : 0;          // compact toggle: click anywhere flips
        if (binding->beginGesture) binding->beginGesture();
        binding->set (binding->min + (float) i);
        if (binding->endGesture) binding->endGesture();
        refresh();
        return;
    }
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
Font nameFont (float h)
{
    static const Typeface::Ptr light = Typeface::createSystemTypefaceFor (ChromaAssets::UrbanistLight_ttf, ChromaAssets::UrbanistLight_ttfSize);
    return Font (FontOptions (light).withHeight (h));
}

Colour rainbow (int k, float t)
{
    // 12 hues red -> violet (one per note of the chromatic scale), vertical shade: light on top, saturated below
    const float hue = (float) jlimit (0, 11, k) / 12.0f * 0.83f;
    const float sat = 0.35f + 0.6f * jlimit (0.0f, 1.0f, t);
    const float bri = 1.0f - 0.15f * jlimit (0.0f, 1.0f, t);
    return Colour::fromHSV (hue, sat, bri, 1.0f);
}

void drawHeader (Graphics& g, Rectangle<float> bar, const String& subtitle, const String& version)
{
    const auto f = nameFont (31.0f).withExtraKerningFactor (0.02f);
    const float base = bar.getCentreY() + 6.0f;

    GlyphArrangement ga;
    ga.addLineOfText (f, "CHROMA5YNTH", bar.getX() + 2.0f, base);
    const float w1 = ga.getBoundingBox (0, -1, true).getWidth();
    g.setColour (style::text.brighter (0.1f));
    ga.draw (g);

    GlyphArrangement gm;
    gm.addLineOfText (style::font (9.5f).withExtraKerningFactor (0.12f), "TMFU audiovibe", bar.getX() + 3.0f, base + 13.0f);
    g.setColour (style::text.withAlpha (0.85f));
    gm.draw (g);

    // 12 rainbow bars, vertically shaded
    const float x = bar.getX() + 2.0f + w1 + 14.0f;
    const float barW = 6.0f, gap = 2.5f, barH = 20.0f, top = base - 11.0f - barH * 0.5f;
    for (int k = 0; k < 12; ++k)
    {
        const auto r = Rectangle<float> (x + (float) k * (barW + gap), top, barW, barH);
        ColourGradient grad (rainbow (k, 0.0f), r.getX(), r.getY(), rainbow (k, 1.0f), r.getX(), r.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, 1.5f);
    }
    const float tx = x + 12.0f * (barW + gap) + 12.0f;

    g.setColour (style::textDim);
    g.setFont (style::font (12.0f));
    g.drawText (subtitle, (int) tx, (int) (top + barH * 0.5f - 10.0f), 400, 20, Justification::centredLeft);
    g.setColour (style::textDim.withAlpha (0.6f));
    g.drawText (version, bar.toNearestInt().withTrimmedRight (6), Justification::centredRight);
}
} // namespace chroma
