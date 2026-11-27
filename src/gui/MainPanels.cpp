#include "MainPanels.h"

namespace chroma::ui
{
using namespace juce;

static void drawPanelFrame (Graphics& g, Rectangle<float> r, const String& title, Colour accent)
{
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
    g.drawFittedText (title.toUpperCase(), h.withTrimmedLeft (10).toNearestInt(), Justification::centredLeft, 1, 0.8f);
}

//============================================================================== meters
static float meterPos (float peak)
{
    const float db = peak > 1.0e-6f ? Decibels::gainToDecibels (peak) : -100.0f;
    return jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);   // -60 .. +6 dBFS
}

void LevelMeter::push (float peak, double dt)
{
    const float target = meterPos (peak);
    level = jmax (target, level - (float) (dt * 0.9));          // ~ 60 dB in 1.1 s
    holdAge += dt;
    if (target >= hold || holdAge > 1.2) { hold = target; holdAge = 0.0; }
    const bool vertical = getHeight() > getWidth();
    const float len = (float) (vertical ? getHeight() : getWidth());
    const int px = roundToInt (level * len), hpx = roundToInt (hold * len);
    if (px != drawnPx || hpx != drawnHoldPx) repaint();
}

void LevelMeter::paint (Graphics& g)
{
    g.fillAll (style::outline);
    const bool vertical = getHeight() > getWidth();
    const float len = (float) (vertical ? getHeight() : getWidth());
    drawnPx = roundToInt (level * len);
    drawnHoldPx = roundToInt (hold * len);
    const float zero = meterPos (1.0f) * len;
    auto bar = [&] (float from, float to, Colour c)
    {
        if (to <= from) return;
        g.setColour (c);
        if (vertical) g.fillRect (0.0f, len - to, (float) getWidth(), to - from);
        else          g.fillRect (from, 0.0f, to - from, (float) getHeight());
    };
    const float lv = (float) drawnPx;
    const float warn = meterPos (Decibels::decibelsToGain (-6.0f)) * len;
    bar (0.0f, jmin (lv, warn), style::accent2);
    bar (warn, jmin (lv, zero), style::accentG);
    bar (zero, lv, style::accent);
    if (drawnHoldPx > 1)
        bar ((float) drawnHoldPx - 1.0f, (float) drawnHoldPx, style::text);
}

StereoMeter::StereoMeter()
{
    addAndMakeVisible (left);
    addAndMakeVisible (right);
}

void StereoMeter::update (Synth& s, double dt)
{
    left.push (s.takeMasterPeak (0), dt);
    right.push (s.takeMasterPeak (1), dt);
    int v = 0;
    for (int l = 0; l < numLayers; ++l) v += s.meterVoiceCount (l);
    if (v != voices) { voices = v; repaint (getLocalBounds().removeFromTop (16)); }
}

void StereoMeter::resized()
{
    auto r = getLocalBounds().withTrimmedTop (18).reduced (2, 0);
    left.setBounds (r.removeFromTop (r.getHeight() / 2 - 1));
    r.removeFromTop (2);
    right.setBounds (r);
}

void StereoMeter::paint (Graphics& g)
{
    g.setFont (style::font (11.0f));
    g.setColour (style::textDim);
    auto t = getLocalBounds().removeFromTop (16).reduced (2, 0);
    g.drawText ("OUT  L / R", t, Justification::centredLeft);
    g.setColour (style::text);
    g.drawText (String (jmax (0, voices)) + " voices", t, Justification::centredRight);
}

//============================================================================== layer tile
BindingPtr hostBindingFor (UiContext& c, int l, const String& suffix);

LayerTile::LayerTile (UiContext& c, int l)
    : ctx (c), layer (l),
      power (hostBindingFor (c, l, "on")), mute (hostBindingFor (c, l, "mute"), "M"), solo (hostBindingFor (c, l, "solo"), "S"),
      engine (hostBindingFor (c, l, "engine"))
{
    power.setTooltip ("Accende / spegne il Layer " + String (l + 1) + " (spento = zero CPU).");
    mute.setTooltip ("Mute del Layer " + String (l + 1) + ".");
    solo.setTooltip ("Solo del Layer " + String (l + 1) + ".");
    mute.onColour = style::accentG;
    solo.onColour = style::accentG;
    power.onColour = style::accent;
    setTooltip ("Layer " + String (l + 1) + ": clic per selezionarlo, clic sull'engine per cambiarlo, clic destro per copia/incolla/preset.");
    for (auto* ch : std::initializer_list<Component*> { &power, &mute, &solo, &meter })
        addAndMakeVisible (ch);
}

void LayerTile::refresh()
{
    power.refresh();
    mute.refresh();
    solo.refresh();
    const int e = roundToInt (engine->get());
    const int v = ctx.synth().meterVoiceCount (layer);
    const bool sel = ctx.layer == layer;
    if (e != engineIndex || v != voices || sel != selected || power.isOn() != shownOn)
    {
        shownOn = power.isOn();
        engineIndex = e;
        voices = v;
        selected = sel;
        repaint();
    }
}

void LayerTile::updateMeter (double dt) { meter.push (ctx.synth().takeLayerPeak (layer), dt); }

Rectangle<int> LayerTile::engineArea() const { return { 4, 22, getWidth() - 8, 16 }; }

void LayerTile::resized()
{
    const int s = 16;
    power.setBounds (getWidth() - s - 4, 3, s, s);
    solo.setBounds (power.getX() - s - 2, 3, s, s);
    mute.setBounds (solo.getX() - s - 2, 3, s, s);
    meter.setBounds (4, getHeight() - 7, getWidth() - 8, 4);
}

void LayerTile::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (selected ? style::header : style::panel);
    g.fillRoundedRectangle (r, style::panelRadius);
    if (selected)
    {
        g.setColour (style::accent);
        g.drawRoundedRectangle (r.reduced (0.5f), style::panelRadius, 1.5f);
    }
    const bool on = power.isOn();
    g.setColour (on ? style::text : style::textDim);
    g.setFont (style::font (15.0f, true));
    g.drawText (String (layer + 1), 6, 2, 26, 20, Justification::centredLeft);
    g.setFont (style::font (11.5f, on));
    g.setColour (on ? (selected ? style::accent : style::text) : style::textDim);
    const auto ea = engineArea();
    g.drawFittedText (String (engineNames[(size_t) jlimit (0, 4, engineIndex)]) + utf8 ("  ▾"), ea, Justification::centredLeft, 1, 0.7f);
    if (voices > 0)
    {
        g.setColour (style::accent);
        g.setFont (style::font (10.5f));
        g.drawText (String (voices), ea, Justification::centredRight);
    }
}

void LayerTile::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onMenu) onMenu (layer, *this);
        return;
    }
    if (onSelect) onSelect (layer);
    if (engineArea().contains (e.getPosition()))
    {
        PopupMenu m;
        m.addSectionHeader ("Engine Layer " + String (layer + 1));
        for (int i = 0; i < (int) engineNames.size(); ++i)
            m.addItem (engineNames[(size_t) i], true, i == engineIndex, [b = engine, i]
            {
                b->beginGesture();
                b->set ((float) i);
                b->endGesture();
            });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
    }
}

BindingPtr hostBindingFor (UiContext& c, int l, const String& suffix)
{
    // fixed layer (tiles), unlike UiContext::hostOfLayer which follows the selection
    const auto id = "layer" + String (l + 1) + "_" + suffix;
    return c.host (id);
}

//============================================================================== scale keyboard
static const char* const scaleKeys[] { "c", "cs", "d", "ds", "e", "f", "fs", "g", "gs", "a", "as", "b" };
static const char* const noteNames[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static bool isBlack (int k) { return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }

ScaleKeyboard::ScaleKeyboard (UiContext& c) : ctx (c)
{
    for (int k = 0; k < 12; ++k)
        slots[(size_t) k] = ctx.hostParam (String ("scale_") + scaleKeys[k]);
    setTooltip (utf8 ("NOTE ASSIGN: clic su una nota per scegliere il synth (1–12) che la suona, oppure Off (nota muta)."));
}

void ScaleKeyboard::refresh()
{
    bool changed = shownLayer != ctx.layer;
    for (size_t k = 0; k < 12; ++k)
    {
        const int m = slots[k] != nullptr ? roundToInt (slots[k]->convertFrom0to1 (slots[k]->getValue())) : 4095;
        changed = changed || m != masks[k];
        masks[k] = m;
    }
    shownLayer = ctx.layer;
    if (changed) repaint();
}

Rectangle<float> ScaleKeyboard::keyRect (int k) const
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float ww = r.getWidth() / 7.0f;
    static const int whiteIndex[] { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    if (! isBlack (k))
        return { r.getX() + ww * (float) whiteIndex[k], r.getY(), ww - 1.5f, r.getHeight() };
    return { r.getX() + ww * (float) (whiteIndex[k] + 1) - ww * 0.32f, r.getY(), ww * 0.64f, r.getHeight() * 0.56f };
}

int ScaleKeyboard::keyAt (Point<float> p) const
{
    for (int k = 0; k < 12; ++k)
        if (isBlack (k) && keyRect (k).contains (p)) return k;
    for (int k = 0; k < 12; ++k)
        if (! isBlack (k) && keyRect (k).contains (p)) return k;
    return -1;
}

static String synthLabel (int mask)
{
    if (mask == 0) return utf8 ("×");
    if (mask == 4095) return "All";
    String s;
    for (int l = 0; l < 12; ++l)
        if ((mask >> l) & 1) s << (s.isEmpty() ? "" : "+") << String (l + 1);
    return s;
}

void ScaleKeyboard::paint (Graphics& g)
{
    const int bit = 1 << ctx.layer;
    for (int pass = 0; pass < 2; ++pass)
        for (int k = 0; k < 12; ++k)
        {
            if (isBlack (k) != (pass == 1)) continue;
            const auto r = keyRect (k);
            const int m = masks[(size_t) k];
            const bool mine = (m & bit) != 0;
            Colour base = isBlack (k) ? style::panel2 : style::track.brighter (0.2f);
            if (m != 0) base = mine ? style::accent : style::accent.withMultipliedSaturation (0.55f).withMultipliedBrightness (0.7f);
            else base = style::outline;
            g.setColour (base);
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (style::outline);
            g.drawRoundedRectangle (r, 2.0f, 1.0f);
            // assigned synth (number) on top, note name at the bottom
            g.setColour (m == 0 ? style::textDim : Colours::black);
            g.setFont (style::font (isBlack (k) ? 10.0f : 11.5f, true));
            g.drawFittedText (synthLabel (m), r.withHeight (isBlack (k) ? r.getHeight() * 0.6f : 22.0f).toNearestInt(),
                              Justification::centred, 1, 0.6f);
            g.setColour (m == 0 ? style::textDim : Colours::black.withAlpha (0.7f));
            g.setFont (style::font (9.5f));
            g.drawText (noteNames[k], r.withTrimmedTop (r.getHeight() - 14.0f), Justification::centred);
        }
}

void ScaleKeyboard::mouseDown (const MouseEvent& e)
{
    const int k = keyAt (e.position);
    if (k < 0 || slots[(size_t) k] == nullptr) return;
    auto* p = slots[(size_t) k];
    auto set = [this, p] (int m)
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) m));
        p->endChangeGesture();
        refresh();
    };
    const int m = masks[(size_t) k];
    PopupMenu menu;
    menu.addSectionHeader (String ("Nota ") + noteNames[k]);
    for (int l = 0; l < 12; ++l)
        menu.addItem ("Synth " + String (l + 1), true, m == (1 << l), [set, l] { set (1 << l); });
    menu.addSeparator();
    menu.addItem (utf8 ("Off (nota muta)"), true, m == 0, [set] { set (0); });
    menu.showMenuAsync (PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (keyRect (k).toNearestInt()))
                                            .withMinimumWidth (110));
}

//============================================================================== additive editor
AdditiveEditor::AdditiveEditor (UiContext& c) : ctx (c)
{
    wide = true;
    for (int f = 0; f < add::numFields; ++f)
        field.addItem (add::fieldInfo (f).name, f + 1);
    field.setSelectedId (add::fAmp + 1, dontSendNotification);
    field.setTooltip ("Campo per parziale da modificare.");
    for (int n : { 16, 32, 64, 128, 256 })
        count.addItem (String (n) + " partials", n);
    count.setSelectedId (64, dontSendNotification);
    count.setTooltip ("Numero di parziali visibili nell'editor.");
    field.onChange = count.onChange = [this] { repaint(); };
    for (auto* b : { &abA, &abB })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (41);
        addAndMakeVisible (b);
    }
    abA.setToggleState (true, dontSendNotification);
    abA.setTooltip ("Modifica lo spettro Custom A.");
    abB.setTooltip ("Modifica lo spettro Custom B.");
    abA.onClick = [this] { ab = 0; repaint(); };
    abB.onClick = [this] { ab = 1; repaint(); };
    shapeButton.setTooltip ("Carica una forma di fabbrica nello spettro Custom selezionato.");
    shapeButton.onClick = [this]
    {
        static const char* const names[] { "Sine", "Saw", "Square", "Triangle", "Pulse 25%", "Flat", "Organ", "Hollow",
                                           "Bell", "Vocal A", "Vocal O", "Strings", "Glass" };
        PopupMenu m;
        for (int s = add::Sine; s < add::numShapes; ++s)
            m.addItem (names[s - 1], [this, s]
            {
                ctx.store().additiveSpectrum (ab).load (add::shapeTable (s));
                ctx.store().touch();
                repaint();
            });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&shapeButton));
    };
    for (auto* ch : std::initializer_list<Component*> { &field, &count, &shapeButton })
        addAndMakeVisible (ch);
    setRepaintsOnMouseActivity (false);
}

void AdditiveEditor::resized()
{
    auto r = getLocalBounds().withTrimmedTop (style::panelHeaderH + 4).reduced (6, 0).removeFromTop (24);
    abA.setBounds (r.removeFromLeft (28));
    r.removeFromLeft (2);
    abB.setBounds (r.removeFromLeft (28));
    r.removeFromLeft (8);
    field.setBounds (r.removeFromLeft (110));
    r.removeFromLeft (8);
    count.setBounds (r.removeFromLeft (120));
    r.removeFromLeft (8);
    shapeButton.setBounds (r.removeFromLeft (96));
}

Rectangle<float> AdditiveEditor::bars() const
{
    return getLocalBounds().toFloat().withTrimmedTop ((float) style::panelHeaderH + 34.0f).reduced (8.0f, 8.0f);
}

float AdditiveEditor::toNorm (int f, float v) const
{
    const auto& i = add::fieldInfo (f);
    if (f == add::fRatio) return std::log (v / i.min) / std::log (i.max / i.min);
    if (f == add::fFreq) return std::sqrt (jmax (0.0f, v) / i.max);
    return (v - i.min) / (i.max - i.min);
}

float AdditiveEditor::fromNorm (int f, float n) const
{
    const auto& i = add::fieldInfo (f);
    n = jlimit (0.0f, 1.0f, n);
    if (f == add::fRatio) return i.min * std::pow (i.max / i.min, n);
    if (f == add::fFreq) return n * n * i.max;
    if (f == add::fPolarity) return n < 0.5f ? -1.0f : 1.0f;
    if (f == add::fMute || f == add::fSolo) return n < 0.5f ? 0.0f : 1.0f;
    return i.min + n * (i.max - i.min);
}

void AdditiveEditor::refresh()
{
    const auto v = ctx.store().additiveSpectrum (ab).version();
    if (v != shownVersion || shownLayer != ctx.layer) { shownVersion = v; shownLayer = ctx.layer; repaint(); }
}

void AdditiveEditor::paint (Graphics& g)
{
    drawPanelFrame (g, getLocalBounds().toFloat(), utf8 ("Additive · Custom Spectrum"), style::module);
    const auto r = bars();
    g.setColour (style::outline);
    g.fillRoundedRectangle (r.expanded (2.0f), style::controlRadius);
    const int f = field.getSelectedId() - 1, n = count.getSelectedId();
    const auto& spec = ctx.store().additiveSpectrum (ab);
    const auto& info = add::fieldInfo (f);
    const bool bipolar = info.min < 0.0f && info.max > 0.0f;
    const float zeroY = bipolar ? r.getBottom() - toNorm (f, 0.0f) * r.getHeight() : r.getBottom();
    const float bw = r.getWidth() / (float) n;
    for (int k = 0; k < n; ++k)
    {
        const float y = r.getBottom() - jlimit (0.0f, 1.0f, toNorm (f, spec.get (f, k))) * r.getHeight();
        const bool muted = spec.get (add::fMute, k) > 0.5f;
        g.setColour (k == hover ? style::accentG : (muted ? style::track : style::module));
        g.fillRect (Rectangle<float> (r.getX() + bw * (float) k + (bw > 4.0f ? 0.5f : 0.0f), jmin (y, zeroY),
                                      jmax (1.0f, bw - (bw > 4.0f ? 1.0f : 0.0f)), jmax (1.0f, std::abs (zeroY - y))));
    }
    if (hover >= 0)
    {
        g.setColour (style::text);
        g.setFont (style::font (11.0f));
        g.drawText ("#" + String (hover + 1) + "  " + info.name + " " + String (spec.get (f, hover), 3),
                    getLocalBounds().withTrimmedTop (style::panelHeaderH + 4).removeFromTop (24).withTrimmedRight (8),
                    Justification::centredRight);
    }
}

void AdditiveEditor::drawAt (Point<float> p, bool line)
{
    const auto r = bars();
    const int f = field.getSelectedId() - 1, n = count.getSelectedId();
    const int k = jlimit (0, n - 1, (int) ((p.x - r.getX()) / r.getWidth() * (float) n));
    const float v = fromNorm (f, (r.getBottom() - p.y) / r.getHeight());
    auto& spec = ctx.store().additiveSpectrum (ab);
    if (line && lastK >= 0 && lastK != k)
    {
        const int step = k > lastK ? 1 : -1;
        for (int i = lastK; i != k; i += step)
        {
            const float t = (float) (i - lastK) / (float) (k - lastK);
            spec.set (f, i, fromNorm (f, toNorm (f, lastV) + (toNorm (f, v) - toNorm (f, lastV)) * t));
        }
    }
    spec.set (f, k, v);
    ctx.store().touch();
    lastK = k;
    lastV = v;
    hover = k;
    repaint();
}

void AdditiveEditor::mouseDown (const MouseEvent& e)
{
    lastK = -1;
    if (bars().contains (e.position)) drawAt (e.position, false);
}

void AdditiveEditor::mouseDrag (const MouseEvent& e)
{
    if (lastK >= 0) drawAt (e.position, true);
}

void AdditiveEditor::mouseMove (const MouseEvent& e)
{
    const auto r = bars();
    const int n = count.getSelectedId();
    const int k = r.contains (e.position) ? jlimit (0, n - 1, (int) ((e.position.x - r.getX()) / r.getWidth() * (float) n)) : -1;
    if (k != hover) { hover = k; repaint(); }
}

//============================================================================== PD curve editor
PdCurveEditor::PdCurveEditor (UiContext& c) : ctx (c)
{
    for (int i = 0; i < 3; ++i)
    {
        interp[i].setClickingTogglesState (true);
        interp[i].setRadioGroupId (42);
        interp[i].onClick = [this, i] { mode = i; publish(); };
        interp[i].setTooltip ("Interpolazione tra i punti della curva Custom.");
        addAndMakeVisible (interp[i]);
    }
    resetButton.setTooltip ("Riporta la curva Custom alla diagonale.");
    resetButton.onClick = [this] { pts = { { 0.0f, 0.0f }, { 1.0f, 1.0f } }; publish(); };
    addAndMakeVisible (resetButton);
}

void PdCurveEditor::resized()
{
    auto r = getLocalBounds().withTrimmedTop (style::panelHeaderH + 4).reduced (6, 0).removeFromTop (22);
    for (auto& b : interp) { b.setBounds (r.removeFromLeft (64)); r.removeFromLeft (2); }
    resetButton.setBounds (r.removeFromRight (56));
}

Rectangle<float> PdCurveEditor::plot() const
{
    return getLocalBounds().toFloat().withTrimmedTop ((float) style::panelHeaderH + 32.0f).reduced (12.0f, 10.0f);
}

void PdCurveEditor::refresh()
{
    const auto v = ctx.store().getVersion();
    if (drag < 0 && (v != shownVersion || shownLayer != ctx.layer))
    {
        shownVersion = v;
        shownLayer = ctx.layer;
        const auto& curve = ctx.store().pdCurve();
        pts = curve.points();
        mode = curve.interpolation();
        for (int i = 0; i < 3; ++i) interp[i].setToggleState (i == mode, dontSendNotification);
        repaint();
    }
}

void PdCurveEditor::publish()
{
    ctx.store().pdCurve().setPoints (pts, mode);
    ctx.store().touch();
    pts = ctx.store().pdCurve().points();
    shownVersion = ctx.store().getVersion();
    repaint();
}

int PdCurveEditor::pointAt (Point<float> p) const
{
    const auto r = plot();
    for (int i = 0; i < (int) pts.size(); ++i)
    {
        const Point<float> q (r.getX() + pts[(size_t) i].x * r.getWidth(), r.getBottom() - pts[(size_t) i].y * r.getHeight());
        if (q.getDistanceFrom (p) < 7.0f) return i;
    }
    return -1;
}

void PdCurveEditor::paint (Graphics& g)
{
    drawPanelFrame (g, getLocalBounds().toFloat(), utf8 ("PD · Custom Phase Curve"), style::module);
    const auto r = plot();
    g.setColour (style::outline);
    g.fillRoundedRectangle (r.expanded (6.0f), style::controlRadius);
    g.setColour (style::track.withAlpha (0.5f));
    for (int i = 1; i < 4; ++i)
    {
        g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) i / 4.0f), r.getX(), r.getRight());
        g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) i / 4.0f), r.getY(), r.getBottom());
    }
    g.drawLine (r.getX(), r.getBottom(), r.getRight(), r.getY(), 1.0f);
    Path p;
    for (int i = 0; i <= 200; ++i)
    {
        const float x = (float) i / 200.0f, y = pd::PhaseCurve::interpolate (pts, mode, x);
        const float px = r.getX() + x * r.getWidth(), py = r.getBottom() - jlimit (0.0f, 1.0f, y) * r.getHeight();
        if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
    }
    g.setColour (style::module);
    g.strokePath (p, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
    for (size_t i = 0; i < pts.size(); ++i)
    {
        const bool fixed = i == 0 || i + 1 == pts.size();
        const Point<float> q (r.getX() + pts[i].x * r.getWidth(), r.getBottom() - pts[i].y * r.getHeight());
        g.setColour (fixed ? style::textDim : ((int) i == drag ? style::accentG : style::text));
        g.fillEllipse (Rectangle<float> (9.0f, 9.0f).withCentre (q));
    }
}

void PdCurveEditor::mouseDown (const MouseEvent& e)
{
    drag = pointAt (e.position);
    if (drag == 0 || drag == (int) pts.size() - 1) drag = -1;   // end points are fixed
    if (drag > 0 && e.mods.isPopupMenu())
    {
        pts.erase (pts.begin() + drag);
        drag = -1;
        publish();
    }
}

void PdCurveEditor::mouseDrag (const MouseEvent& e)
{
    if (drag <= 0) return;
    const auto r = plot();
    auto& q = pts[(size_t) drag];
    const float lo = pts[(size_t) drag - 1].x + 0.002f, hi = pts[(size_t) drag + 1].x - 0.002f;
    q.x = jlimit (lo, jmax (lo, hi), (e.position.x - r.getX()) / r.getWidth());
    q.y = jlimit (0.0f, 1.0f, (r.getBottom() - e.position.y) / r.getHeight());
    publish();
}

void PdCurveEditor::mouseUp (const MouseEvent&) { drag = -1; repaint(); }

void PdCurveEditor::mouseDoubleClick (const MouseEvent& e)
{
    const int i = pointAt (e.position);
    if (i > 0 && i < (int) pts.size() - 1)
        pts.erase (pts.begin() + i);
    else if (i < 0 && plot().contains (e.position) && (int) pts.size() < pd::PhaseCurve::maxPoints)
    {
        const auto r = plot();
        pts.push_back ({ (e.position.x - r.getX()) / r.getWidth(), (r.getBottom() - e.position.y) / r.getHeight() });
    }
    publish();
}

//============================================================================== tools
ToolsBlock::ToolsBlock (const String& t, String i) : title (t), info (std::move (i)) {}

void ToolsBlock::addButton (const String& text, const String& tip, std::function<void (Component&)> fn)
{
    auto b = std::make_unique<TextButton> (text);
    b->setTooltip (tip);
    auto* raw = b.get();
    b->onClick = [raw, fn] { fn (*raw); };
    addAndMakeVisible (*b);
    buttons.push_back (std::move (b));
}

void ToolsBlock::resized()
{
    auto r = getLocalBounds().withTrimmedTop (style::panelHeaderH + 8).reduced (8, 0).removeFromTop (26);
    for (auto& b : buttons) { b->setBounds (r.removeFromLeft (120)); r.removeFromLeft (6); }
}

void ToolsBlock::paint (Graphics& g)
{
    drawPanelFrame (g, getLocalBounds().toFloat(), title, style::module);
    g.setColour (style::textDim);
    g.setFont (style::font (11.0f));
    g.drawFittedText (info, getLocalBounds().withTrimmedTop (style::panelHeaderH + 38).reduced (8, 0).removeFromTop (22),
                      Justification::centredLeft, 1, 0.8f);
}
} // namespace chroma::ui
