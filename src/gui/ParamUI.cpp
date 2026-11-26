#include "ParamUI.h"
#include "GuiLayout.h"

namespace chroma::ui
{
using namespace juce;

//============================================================================== bindings
static void fillHostDescriptor (Binding& b, RangedAudioParameter& p)
{
    b.name = p.getName (64);
    b.unit = p.getLabel();
    const auto& r = p.getNormalisableRange();
    b.min = r.start;
    b.max = r.end;
    b.def = p.convertFrom0to1 (p.getDefaultValue());
    if (auto* c = dynamic_cast<AudioParameterChoice*> (&p))
    {
        b.kind = Binding::Kind::Choice;
        b.choices = c->choices;
    }
    else if (dynamic_cast<AudioParameterBool*> (&p) != nullptr)
    {
        b.kind = Binding::Kind::Bool;
        b.choices = { "Off", "On" };
    }
    else if (dynamic_cast<AudioParameterInt*> (&p) != nullptr)
        b.kind = Binding::Kind::Int;
    else
    {
        b.kind = Binding::Kind::Float;
        b.hostRange = r;
        b.hasHostRange = true;
    }
}

static BindingPtr hostBinding (std::function<RangedAudioParameter*()> fn, const String& tooltip)
{
    auto* p0 = fn();
    if (p0 == nullptr)
        return nullptr;
    auto b = std::make_shared<Binding>();
    fillHostDescriptor (*b, *p0);
    b->tooltip = tooltip;
    b->get = [fn] { auto* p = fn(); return p->convertFrom0to1 (p->getValue()); };
    b->set = [fn] (float v)
    {
        auto* p = fn();
        const float n = p->convertTo0to1 (v);
        if (std::abs (n - p->getValue()) > 1.0e-7f) p->setValueNotifyingHost (n);
    };
    b->beginGesture = [fn] { fn()->beginChangeGesture(); };
    b->endGesture = [fn] { fn()->endChangeGesture(); };
    return b;
}

static String hostTooltip (const String& id)
{
    for (const auto& e : gui::params)
        if (id == e.id) return utf8 (e.description);
    return {};
}

BindingPtr UiContext::host (const String& id)
{
    auto* p = hostParam (id);
    return hostBinding ([p] { return p; }, hostTooltip (id));
}

BindingPtr UiContext::hostOfLayer (const String& suffix)
{
    std::array<RangedAudioParameter*, numLayers> ps {};
    for (int l = 0; l < numLayers; ++l)
        if ((ps[(size_t) l] = layerHostParam (l, suffix)) == nullptr)
            return nullptr;
    auto b = hostBinding ([this, ps] { return ps[(size_t) layer]; }, hostTooltip ("layer1_" + suffix).replace ("Layer 1", "Layer"));
    b->name = b->name.fromFirstOccurrenceOf (" ", false, false);   // "L1 Volume" -> "Volume"
    return b;
}

BindingPtr UiContext::layerParam (int id)
{
    const auto& d = lp::descs[(size_t) id];
    auto b = std::make_shared<Binding>();
    b->name = utf8 (d.name);
    b->unit = utf8 (d.unit);
    b->tooltip = utf8 (d.description);
    b->min = d.min;
    b->max = d.max;
    b->def = d.def;
    b->skewCentre = d.skewCentre;
    b->hasSkew = d.hasSkew;
    switch (d.type)
    {
        case lp::Type::Float:  b->kind = Binding::Kind::Float; break;
        case lp::Type::Int:    b->kind = Binding::Kind::Int; break;
        case lp::Type::Bool:   b->kind = Binding::Kind::Bool; b->choices = { "Off", "On" }; break;
        case lp::Type::Choice: b->kind = Binding::Kind::Choice;
            for (int i = 0; i < d.numChoices; ++i) b->choices.add (utf8 (d.choices[i]));
            break;
    }
    if (d.choices == lp::list_modDestinations)
    {
        b->groups.add ({});
        for (auto destId : lp::modDestinations)
            b->groups.add (utf8 (lp::descs[(size_t) destId].section).replace (utf8 ("Layer · "), "").upToFirstOccurrenceOf (utf8 (" · "), false, false));
    }
    b->get = [this, id] { return store().get (id); };
    b->set = [this, id] (float v) { store().set (id, v); };
    return b;
}

void UiContext::showMenu (const String& hostId, int lpId, BindingPtr b, Component& target)
{
    auto& s = synth();
    ParamTarget t;
    if (lpId >= 0) t = { layer, lpId };
    else if (hostId.isNotEmpty()) t = { ParamTarget::host, s.hostParamIndex (hostId) };
    if (t.layer == ParamTarget::host && t.id < 0) t = {};

    PopupMenu m;
    m.addSectionHeader (b != nullptr ? b->name : String ("Parametro"));
    if (b != nullptr)
        m.addItem ("Ripristina valore predefinito", [b]
        {
            if (b->beginGesture) b->beginGesture();
            b->set (b->def);
            if (b->endGesture) b->endGesture();
        });
    if (t.isValid())
    {
        const int cc = s.midiMappingFor (t);
        m.addSeparator();
        m.addItem ("MIDI Learn (muovi un controller)", [&s, t] { s.startMidiLearn (t); });
        m.addItem (cc >= 0 ? "Rimuovi MIDI Learn (CC " + String (cc) + ")" : String ("Rimuovi MIDI Learn"), cc >= 0, false,
                   [&s, t] { s.removeMidiMapping (t); });
        if (s.isMidiLearning()) m.addItem ("Annulla MIDI Learn", [&s] { s.cancelMidiLearn(); });

        const bool isMacro = hostId.startsWith ("macro");
        if (! isMacro)
        {
            PopupMenu macros;
            const auto& map = s.getMacroMap();
            for (int k = 0; k < numMacros; ++k)
            {
                bool assigned = false;
                for (int i = 0; i < map.count; ++i)
                    assigned = assigned || (map.items[(size_t) i].macro == k && map.items[(size_t) i].target == t);
                macros.addItem ("Macro " + String (k + 1), true, assigned, [&s, k, t] { s.assignMacro (k, t, 0.0f, 1.0f); });
            }
            m.addSubMenu ("Assegna a Macro", macros);
        }
        else
        {
            const int k = hostId.fromFirstOccurrenceOf ("macro", false, false).getIntValue() - 1;
            m.addItem ("Svuota assegnazioni Macro " + String (k + 1), [&s, k] { s.clearMacroAssignments (k); });
        }

        if (t.layer >= 0)
        {
            int slot = -1;
            for (int i = 0; i < numAutomationSlots && slot < 0; ++i)
                if (s.getAutomationTarget (i) == t) slot = i;
            if (slot >= 0)
                m.addItem ("Rimuovi slot automazione host " + String (slot + 1), [&s, slot] { s.clearAutomation (slot); });
            else
                m.addItem ("Esponi all'automazione host", [&s, t]
                {
                    if (s.assignAutomation (t.layer, t.id) < 0)
                        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Automazione", "Nessuno slot di automazione libero.");
                });
        }
    }
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&target));
}

//============================================================================== widgets
struct ParamWidget::Combo final : ComboBox
{
    explicit Combo (BindingPtr b) : binding (std::move (b))
    {
        if (binding->groups.size() == binding->choices.size())
        {
            auto* root = getRootMenu();
            String current;
            PopupMenu sub;
            auto flush = [&]
            {
                if (current.isNotEmpty()) root->addSubMenu (current, sub);
                sub = PopupMenu();
            };
            for (int i = 0; i < binding->choices.size(); ++i)
            {
                const auto& g = binding->groups[i];
                if (g.isEmpty()) { root->addItem (i + 1, binding->choices[i]); continue; }
                if (g != current) { flush(); current = g; }
                sub.addItem (i + 1, binding->choices[i]);
            }
            flush();
        }
        else
            addItemList (binding->choices, 1);
        setTooltip (binding->tooltip.isNotEmpty() ? binding->tooltip : binding->name);
        onChange = [this]
        {
            if (getSelectedId() <= 0) return;
            const float v = binding->min + (float) (getSelectedId() - 1);
            if (std::abs (v - binding->get()) < 0.5f) return;
            if (binding->beginGesture) binding->beginGesture();
            binding->set (v);
            if (binding->endGesture) binding->endGesture();
        };
        refresh();
    }
    void refresh()
    {
        const int id = roundToInt (binding->get() - binding->min) + 1;
        if (id != getSelectedId()) setSelectedId (id, dontSendNotification);
    }
    BindingPtr binding;
};

// Horizontal bar slider with the value printed inside (tables).
struct ParamWidget::Bar final : FineSlider
{
    explicit Bar (BindingPtr b) : binding (std::move (b))
    {
        setSliderStyle (LinearBar);
        setTextBoxStyle (NoTextBox, false, 0, 0);
        setNormalisableRange (binding->range());
        setDoubleClickReturnValue (true, binding->def);
        setValue (binding->get(), dontSendNotification);
        setTooltip (binding->tooltip.isNotEmpty() ? binding->tooltip : binding->name);
        dragPixels = 300;
        setMouseDragSensitivity (dragPixels);
        onDragStart = [this] { if (binding->beginGesture) binding->beginGesture(); };
        onDragEnd = [this] { if (binding->endGesture) binding->endGesture(); };
        onValueChange = [this] { binding->set ((float) getValue()); repaint(); };
    }
    void refresh()
    {
        if (isMouseButtonDown()) return;
        const double v = binding->get();
        if (std::abs (v - getValue()) > 1.0e-7) setValue (v, dontSendNotification);
    }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f, 1.0f);
        g.setColour (style::outline);
        g.fillRoundedRectangle (r, style::controlRadius);
        const float p = (float) valueToProportionOfLength (getValue());
        const bool bipolar = binding->min < 0.0f && binding->max > 0.0f;
        const float z = bipolar ? (float) valueToProportionOfLength (0.0) : 0.0f;
        auto in = r.reduced (1.5f);
        const float x0 = in.getX() + in.getWidth() * jmin (p, z), x1 = in.getX() + in.getWidth() * jmax (p, z);
        g.setColour (style::accent.withAlpha (isMouseOverOrDragging() ? 0.9f : 0.65f));
        g.fillRoundedRectangle (Rectangle<float> (x0, in.getY(), jmax (1.5f, x1 - x0), in.getHeight()), 1.5f);
        g.setColour (style::text);
        g.setFont (style::font (11.0f));
        g.drawFittedText (binding->format ((float) getValue()), getLocalBounds(), Justification::centred, 1, 0.7f);
    }
    BindingPtr binding;
};

ParamWidget::ParamWidget (UiContext& c, BindingPtr b, String hid, int lid, const String& cap, Colour accent, bool compact)
    : ctx (c), binding (std::move (b)), hostId (std::move (hid)), caption (cap), lpId (lid)
{
    const bool discrete = binding->kind == Binding::Kind::Choice || binding->kind == Binding::Kind::Bool;
    const int n = binding->choices.size();
    if (compact && ! discrete)
    {
        bar = std::make_unique<Bar> (binding);
        addAndMakeVisible (*bar);
    }
    else if (discrete && (n > 5 || compact))
    {
        combo = std::make_unique<Combo> (binding);
        prefW = comboW;
        addAndMakeVisible (*combo);
    }
    else if (discrete)
    {
        seg = std::make_unique<Segmented> (accent);
        seg->bind (binding);
        int textW = 0;
        for (const auto& s : binding->choices)
            textW += jmax (22, GlyphArrangement::getStringWidthInt (style::font (11.0f, true), s) + 10);
        prefW = jlimit (56, 240, textW + 4);
        addAndMakeVisible (*seg);
    }
    else
    {
        knob = std::make_unique<Knob> (caption, accent);
        knob->bind (binding);
        addAndMakeVisible (*knob);
    }
    for (auto* child : getChildren())
        child->addMouseListener (this, true);
}

ParamWidget::~ParamWidget()
{
    for (auto* child : getChildren())
        child->removeMouseListener (this);
}

void ParamWidget::refresh()
{
    if (knob != nullptr) knob->refresh();
    if (seg != nullptr) seg->refresh();
    if (combo != nullptr) combo->refresh();
    if (bar != nullptr) bar->refresh();
}

void ParamWidget::resized()
{
    auto r = getLocalBounds();
    if (caption.isEmpty() && knob == nullptr)
    {
        for (auto* c : getChildren()) c->setBounds (r);
        return;
    }
    if (knob != nullptr)
        knob->setBounds (r);
    else
    {
        auto mid = r.withSizeKeepingCentre (r.getWidth(), 44);
        mid.removeFromTop (16);
        if (seg != nullptr) seg->setBounds (mid.withSizeKeepingCentre (mid.getWidth(), 24));
        if (combo != nullptr) combo->setBounds (mid.withSizeKeepingCentre (mid.getWidth(), 24));
    }
}

void ParamWidget::paint (Graphics& g)
{
    if (knob != nullptr || caption.isEmpty())
        return;
    auto r = getLocalBounds().withSizeKeepingCentre (getWidth(), 44).removeFromTop (15);
    g.setColour (style::textDim);
    g.setFont (style::font (11.0f));
    g.drawFittedText (caption, r, Justification::centred, 1, 0.7f);
}

void ParamWidget::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        ctx.showMenu (hostId, lpId, binding, *this);
}

//============================================================================== power button
void PowerButton::refresh()
{
    const bool v = binding != nullptr && binding->get() > 0.5f * (binding->min + binding->max);
    if (v != on) { on = v; repaint(); }
}

void PowerButton::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    if (text.isEmpty())
    {
        const float d = jmin (r.getWidth(), r.getHeight());
        auto c = r.withSizeKeepingCentre (d, d);
        g.setColour (on ? onColour : style::panel2);
        g.fillEllipse (c);
        g.setColour (on ? Colours::black : style::textDim);
        const auto ic = c.reduced (d * 0.27f);
        Path p;
        p.addCentredArc (ic.getCentreX(), ic.getCentreY(), ic.getWidth() * 0.5f, ic.getHeight() * 0.5f, 0.0f,
                         MathConstants<float>::pi * 0.22f, MathConstants<float>::pi * 1.78f, true);
        g.strokePath (p, PathStrokeType (jmax (1.2f, d * 0.09f), PathStrokeType::curved, PathStrokeType::rounded));
        g.drawLine (ic.getCentreX(), ic.getY() - d * 0.04f, ic.getCentreX(), ic.getCentreY(), jmax (1.2f, d * 0.09f));
        return;
    }
    g.setColour (on ? onColour : style::panel2);
    g.fillRoundedRectangle (r, style::controlRadius);
    g.setColour (on ? Colours::black : style::textDim);
    g.setFont (style::font (jmin (11.5f, r.getHeight() * 0.7f), true));
    g.drawText (text, r, Justification::centred);
}

void PowerButton::mouseDown (const MouseEvent& e)
{
    if (binding == nullptr || e.mods.isPopupMenu()) return;
    if (binding->beginGesture) binding->beginGesture();
    binding->set (on ? binding->min : binding->max);
    if (binding->endGesture) binding->endGesture();
    refresh();
}

//============================================================================== displays
namespace
{
// AHDSR shape preview (sqrt time axis); sustain drawn as a fixed-width plateau.
class EnvView final : public Component
{
public:
    EnvView (UiContext& c, int a, int h, int d, int s, int r) : ctx (c), ids { a, h, d, s, r } {}
    void refresh()
    {
        std::array<float, 5> v {};
        for (size_t i = 0; i < 5; ++i) v[i] = ids[i] >= 0 ? ctx.store().get (ids[i]) : 0.0f;
        if (v != last) { last = v; repaint(); }
    }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f, 4.0f);
        g.setColour (style::outline);
        g.fillRoundedRectangle (r, style::controlRadius);
        r = r.reduced (6.0f, 6.0f);
        const auto& sustainDesc = lp::descs[(size_t) ids[3]];
        const float sus = jlimit (0.0f, 1.0f, (last[3] - sustainDesc.min) / jmax (1.0e-6f, sustainDesc.max - sustainDesc.min));
        auto t = [] (float x) { return std::sqrt (jmax (0.0f, x)) + 0.02f; };
        const float ta = t (last[0]), th = ids[1] >= 0 ? t (last[1]) - 0.02f : 0.0f, td = t (last[2]), tr = t (last[4]);
        const float total = ta + th + td + tr;
        const float plateau = 0.22f;
        const float k = r.getWidth() * (1.0f - plateau) / jmax (1.0e-3f, total);
        float x = r.getX();
        const float y0 = r.getBottom(), y1 = r.getY();
        const float ys = y0 - sus * r.getHeight();
        Path p;
        p.startNewSubPath (x, y0);
        x += ta * k; p.lineTo (x, y1);
        x += th * k; p.lineTo (x, y1);
        x += td * k; p.quadraticTo (x - td * k * 0.6f, ys, x, ys);
        x += r.getWidth() * plateau; p.lineTo (x, ys);
        x += tr * k; p.quadraticTo (x - tr * k * 0.6f, y0, x, y0);
        Path fill (p);
        fill.closeSubPath();
        g.setColour (style::accent.withAlpha (0.16f));
        g.fillPath (fill);
        g.setColour (style::accent);
        g.strokePath (p, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
    }

private:
    UiContext& ctx;
    std::array<int, 5> ids;
    std::array<float, 5> last { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
};

// One cycle of the LFO shape (Custom: the layer's drawn table).
class LfoView final : public Component
{
public:
    LfoView (UiContext& c, int shape, int lfo) : ctx (c), shapeId (shape), lfoIndex (lfo) {}
    void refresh()
    {
        const int s = roundToInt (ctx.store().get (shapeId));
        if (s != last || s == 7 || ctx.layer != lastLayer) { last = s; lastLayer = ctx.layer; repaint(); }
    }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f, 4.0f);
        g.setColour (style::outline);
        g.fillRoundedRectangle (r, style::controlRadius);
        r = r.reduced (6.0f, 6.0f);
        g.setColour (style::track);
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
        Path p;
        const int n = 128;
        Random rnd (7);
        float held = 0.0f;
        std::array<float, 9> steps {};
        for (auto& v : steps) v = rnd.nextFloat() * 2.0f - 1.0f;
        for (int i = 0; i <= n; ++i)
        {
            const float ph = (float) i / (float) n;
            float v = 0.0f;
            switch (last)
            {
                case 0: v = std::sin (MathConstants<float>::twoPi * ph); break;
                case 1: v = ph < 0.25f ? ph * 4.0f : ph < 0.75f ? 2.0f - ph * 4.0f : ph * 4.0f - 4.0f; break;
                case 2: v = ph < 0.5f ? 1.0f : -1.0f; break;
                case 3: v = ph * 2.0f - 1.0f; break;
                case 4: v = 1.0f - ph * 2.0f; break;
                case 5: if (i % 16 == 0) held = steps[(size_t) (i / 16)]; v = held; break;
                case 6:
                {
                    const float f = ph * 8.0f;
                    const int k = jmin (7, (int) f);
                    const float u = f - (float) k, w = u * u * (3.0f - 2.0f * u);
                    v = steps[(size_t) k] + (steps[(size_t) k + 1] - steps[(size_t) k]) * w;
                    break;
                }
                default:
                {
                    auto& tab = ctx.store().customTable (lfoIndex);
                    const float f = ph * (float) (lfoCustomPoints - 1);
                    const int k = jmin (lfoCustomPoints - 2, (int) f);
                    const float a = tab[(size_t) k].load (std::memory_order_relaxed), b = tab[(size_t) k + 1].load (std::memory_order_relaxed);
                    v = a + (b - a) * (f - (float) k);
                }
            }
            const float x = r.getX() + ph * r.getWidth(), y = r.getCentreY() - v * r.getHeight() * 0.46f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (style::accent2);
        g.strokePath (p, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
    }

private:
    UiContext& ctx;
    int shapeId, lfoIndex, last = -1, lastLayer = -1;
};
} // namespace

//============================================================================== section panel
SectionPanel::SectionPanel (UiContext& c, const String& t, Colour a) : ctx (c), title (t), accent (a) {}

void SectionPanel::addWidget (std::unique_ptr<ParamWidget> w)
{
    cellW.push_back (w->preferredWidth());
    addAndMakeVisible (*w);
    cells.push_back (std::move (w));
}

void SectionPanel::addLayerParam (int id, const String& caption)
{
    addWidget (std::make_unique<ParamWidget> (ctx, ctx.layerParam (id), String(), id, caption, accent));
}

void SectionPanel::addHostParam (const String& id, const String& caption)
{
    if (auto b = ctx.host (id))
        addWidget (std::make_unique<ParamWidget> (ctx, b, id, -1, caption.isNotEmpty() ? caption : b->name, accent));
}

void SectionPanel::addHostOfLayer (const String& suffix, const String& caption)
{
    if (auto b = ctx.hostOfLayer (suffix))
    {
        auto w = std::make_unique<ParamWidget> (ctx, b, String(), -1, caption.isNotEmpty() ? caption : b->name, accent);
        addWidget (std::move (w));
    }
}

void SectionPanel::addExtra (std::unique_ptr<Component> c, int width)
{
    cellW.push_back (width);
    addAndMakeVisible (*c);
    cells.push_back (std::move (c));
}

void SectionPanel::setHeaderToggle (BindingPtr b)
{
    toggle = std::make_unique<PowerButton> (std::move (b));
    toggle->onColour = accent;
    toggle->setTooltip ("Attiva / disattiva la sezione (spenta = zero CPU).");
    addAndMakeVisible (*toggle);
}

void SectionPanel::setDisplay (std::unique_ptr<Component> d, int h)
{
    display = std::move (d);
    displayH = h;
    addAndMakeVisible (*display);
}

int SectionPanel::layoutCells (int width, bool apply)
{
    const int innerW = jmax (knobW, width - 12);
    const int top = style::panelHeaderH + 4 + displayH;
    int x = 0, y = 0;
    std::vector<std::pair<size_t, size_t>> rows;   // [begin, end) per row
    size_t rowStart = 0;
    for (size_t i = 0; i < cells.size(); ++i)
    {
        const int w = jmin (innerW, cellW[i]);
        if (x > 0 && x + w > innerW)
        {
            rows.push_back ({ rowStart, i });
            rowStart = i;
            x = 0;
        }
        x += w + 4;
    }
    if (rowStart < cells.size()) rows.push_back ({ rowStart, cells.size() });
    if (apply)
    {
        for (const auto& [b, e] : rows)
        {
            // spread the row's free space evenly between cells
            int used = 0;
            for (size_t i = b; i < e; ++i) used += jmin (innerW, cellW[i]);
            const int gaps = (int) (e - b);
            const int free = jmax (0, innerW - used - 4 * (gaps - 1));
            const int extra = (e - b) > 1 || cellW[b] > knobW ? jmin (free / jmax (1, gaps), 24) : 0;
            x = 6;
            for (size_t i = b; i < e; ++i)
            {
                const int w = jmin (innerW, cellW[i]) + extra;
                cells[i]->setBounds (x, top + y, w, cellH);
                x += w + 4;
            }
            y += cellH;
        }
    }
    return top + (int) rows.size() * cellH + 6;
}

int SectionPanel::heightFor (int width) { return layoutCells (width, false); }

void SectionPanel::resized()
{
    if (toggle != nullptr)
        toggle->setBounds (getWidth() - style::panelHeaderH - 2, 2, style::panelHeaderH - 4, style::panelHeaderH - 4);
    if (display != nullptr)
        display->setBounds (4, style::panelHeaderH + 2, getWidth() - 8, displayH);
    layoutCells (getWidth(), true);
}

void SectionPanel::refresh()
{
    for (auto& c : cells)
        if (auto* w = dynamic_cast<ParamWidget*> (c.get())) w->refresh();
    if (toggle != nullptr)
    {
        const bool was = toggle->isOn();
        toggle->refresh();
        if (was != toggle->isOn()) repaint();
        for (auto& c : cells) c->setAlpha (toggle->isOn() ? 1.0f : 0.45f);
    }
    if (auto* e = dynamic_cast<EnvView*> (display.get())) e->refresh();
    if (auto* l = dynamic_cast<LfoView*> (display.get())) l->refresh();
}

void SectionPanel::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (style::panel);
    g.fillRoundedRectangle (r, style::panelRadius);
    auto h = r.removeFromTop ((float) style::panelHeaderH);
    g.setColour (style::header);
    g.fillRoundedRectangle (h, style::panelRadius);
    g.fillRect (h.withTrimmedTop (h.getHeight() * 0.5f));
    const bool on = toggle == nullptr || toggle->isOn();
    g.setColour (on ? accent : style::track);
    g.fillRect (Rectangle<float> (h.getX(), h.getY() + 4.0f, 3.0f, h.getHeight() - 8.0f));
    g.setColour (on ? style::text : style::textDim);
    g.setFont (style::font (12.0f, true));
    g.drawFittedText (title.toUpperCase(), h.withTrimmedLeft (10).withTrimmedRight (toggle != nullptr ? 26 : 10).toNearestInt(),
                      Justification::centredLeft, 1, 0.8f);
}

//============================================================================== section factories
static String commonPrefix (const StringArray& names)
{
    if (names.size() < 2) return {};
    auto words = StringArray::fromTokens (names[0], " ", "");
    int n = words.size() - 1;
    for (const auto& s : names)
    {
        auto w = StringArray::fromTokens (s, " ", "");
        int k = 0;
        while (k < n && k < w.size() - 1 && w[k] == words[k]) ++k;
        n = k;
    }
    words.removeRange (n, words.size());
    return words.joinIntoString (" ");
}

std::unique_ptr<SectionPanel> makeLayerSection (UiContext& ctx, const String& section, const String& title, Colour accent)
{
    auto panel = std::make_unique<SectionPanel> (ctx, title, accent);
    std::vector<int> ids;
    StringArray names;
    for (int i = 0; i < lp::count; ++i)
        if (section == utf8 (lp::descs[(size_t) i].section)) { ids.push_back (i); names.add (utf8 (lp::descs[(size_t) i].name)); }
    if (ids.empty()) return panel;
    panel->wide = ids.size() > 28;

    size_t first = 0;
    const auto& d0 = lp::descs[(size_t) ids[0]];
    if (d0.type == lp::Type::Bool && String (d0.id).endsWith ("_on"))
    {
        panel->setHeaderToggle (ctx.layerParam (ids[0]));
        first = 1;
        names.remove (0);
    }
    const auto prefix = commonPrefix (names);

    int a = -1, h = -1, d = -1, s = -1, r = -1, shape = -1;
    for (size_t k = first; k < ids.size(); ++k)
    {
        const String id (lp::descs[(size_t) ids[k]].id);
        String cap = utf8 (lp::descs[(size_t) ids[k]].name);
        if (prefix.isNotEmpty() && cap.startsWith (prefix + " ")) cap = cap.substring (prefix.length() + 1);
        else if (cap.startsWithIgnoreCase (title + " ")) cap = cap.substring (title.length() + 1);
        panel->addLayerParam (ids[k], cap);
        if (id.endsWith ("_attack")) a = ids[k];
        else if (id.endsWith ("_hold")) h = ids[k];
        else if (id.endsWith ("_decay")) d = ids[k];
        else if (id.endsWith ("_sustain")) s = ids[k];
        else if (id.endsWith ("_release")) r = ids[k];
        else if (id.endsWith ("_shape") && id.startsWith ("lfo")) shape = ids[k];
    }
    if (a >= 0 && d >= 0 && s >= 0 && r >= 0)
        panel->setDisplay (std::make_unique<EnvView> (ctx, a, h, d, s, r), 64);
    else if (shape >= 0)
        panel->setDisplay (std::make_unique<LfoView> (ctx, shape, String (lp::descs[(size_t) shape].id).substring (3, 4).getIntValue() - 1), 64);
    return panel;
}

std::unique_ptr<SectionPanel> makeHostSection (UiContext& ctx, const String& section, const String& title, Colour accent)
{
    auto panel = std::make_unique<SectionPanel> (ctx, title, accent);
    StringArray ids, names;
    for (const auto& e : gui::params)
        if (section == utf8 (e.section) && ctx.hostParam (e.id) != nullptr) { ids.add (e.id); names.add (utf8 (e.name)); }
    int first = 0;
    if (! ids.isEmpty() && ids[0].endsWith ("_on") && dynamic_cast<AudioParameterBool*> (ctx.hostParam (ids[0])) != nullptr)
    {
        panel->setHeaderToggle (ctx.host (ids[0]));
        first = 1;
        names.remove (0);
    }
    const auto prefix = commonPrefix (names);
    for (int k = first; k < ids.size(); ++k)
    {
        auto cap = names[k - first];
        if (prefix.isNotEmpty() && cap.startsWith (prefix + " ")) cap = cap.substring (prefix.length() + 1);
        panel->addHostParam (ids[k], cap);
    }
    return panel;
}

//============================================================================== mod matrix
ModMatrixBlock::ModMatrixBlock (UiContext& c) : ctx (c)
{
    wide = true;
    auto id = [] (int slot, const char* field)
    {
        const String key = "mm" + String (slot + 1) + "_" + field;
        for (int i = 0; i < lp::count; ++i)
            if (key == lp::descs[(size_t) i].id) return i;
        jassertfalse;
        return 0;
    };
    for (int s = 0; s < 16; ++s)
    {
        auto en = std::make_unique<PowerButton> (ctx.layerParam (id (s, "enable")), String (s + 1));
        en->setTooltip ("Attiva / disattiva lo slot " + String (s + 1) + ".");
        toggles.push_back (en.get());
        addAndMakeVisible (*en);
        row[s].push_back (std::move (en));
        for (const char* f : { "source", "cc", "dest", "amount", "via", "curve", "smooth" })
        {
            const int lid = id (s, f);
            auto w = std::make_unique<ParamWidget> (ctx, ctx.layerParam (lid), String(), lid, String(), style::accent, true);
            widgets.push_back (w.get());
            addAndMakeVisible (*w);
            row[s].push_back (std::move (w));
        }
        auto inv = std::make_unique<PowerButton> (ctx.layerParam (id (s, "invert")), "INV");
        inv->setTooltip ("Inverte la modulazione.");
        toggles.push_back (inv.get());
        addAndMakeVisible (*inv);
        row[s].push_back (std::move (inv));
    }
}

void ModMatrixBlock::refresh()
{
    for (auto* w : widgets) w->refresh();
    for (auto* t : toggles) t->refresh();
}

static const std::array<float, 9> mmCols { 0.04f, 0.17f, 0.08f, 0.24f, 0.13f, 0.12f, 0.09f, 0.08f, 0.05f };
static const char* const mmTitles[] { "#", "Source", "CC", "Destination", "Amount", "Via", "Curve", "Smooth", "Inv" };

void ModMatrixBlock::resized()
{
    const int w = getWidth() - 12;
    for (int s = 0; s < 16; ++s)
    {
        float x = 6.0f;
        const int y = style::panelHeaderH + 22 + s * 26;
        for (size_t c = 0; c < row[s].size(); ++c)
        {
            const int cw = (int) (mmCols[c] * (float) w);
            row[s][c]->setBounds (Rectangle<int> ((int) x, y, cw - 4, 26).reduced (0, 2));
            x += (float) cw;
        }
    }
}

void ModMatrixBlock::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (style::panel);
    g.fillRoundedRectangle (r, style::panelRadius);
    auto h = r.removeFromTop ((float) style::panelHeaderH);
    g.setColour (style::header);
    g.fillRoundedRectangle (h, style::panelRadius);
    g.fillRect (h.withTrimmedTop (h.getHeight() * 0.5f));
    g.setColour (style::accent2);
    g.fillRect (Rectangle<float> (h.getX(), h.getY() + 4.0f, 3.0f, h.getHeight() - 8.0f));
    g.setColour (style::text);
    g.setFont (style::font (12.0f, true));
    g.drawText ("MOD MATRIX", h.withTrimmedLeft (10), Justification::centredLeft);
    g.setColour (style::textDim);
    g.setFont (style::font (11.0f));
    float x = 6.0f;
    const float w = (float) getWidth() - 12.0f;
    for (size_t c = 0; c < mmCols.size(); ++c)
    {
        g.drawText (mmTitles[c], Rectangle<float> (x, (float) style::panelHeaderH + 3.0f, mmCols[c] * w - 4.0f, 16.0f), Justification::centred);
        x += mmCols[c] * w;
    }
    for (int s = 1; s < 16; s += 2)
    {
        g.setColour (style::bg.withAlpha (0.35f));
        g.fillRect (4.0f, (float) (style::panelHeaderH + 22 + s * 26), (float) getWidth() - 8.0f, 26.0f);
    }
}

//============================================================================== page
Page::Page()
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    viewport.getVerticalScrollBar().setColour (ScrollBar::thumbColourId, style::track);
    addAndMakeVisible (viewport);
}

void Page::add (std::unique_ptr<Block> b)
{
    content.addAndMakeVisible (*b);
    blocks.push_back (std::move (b));
}

void Page::clear()
{
    blocks.clear();
    viewport.setViewPosition (0, 0);
}

void Page::refresh()
{
    if (! isVisible()) return;
    const auto view = viewport.getViewArea();
    for (auto& b : blocks)
        if (b->getBounds().intersects (view)) b->refresh();
}

void Page::resized()
{
    viewport.setBounds (getLocalBounds());
    const int w = getWidth() - viewport.getScrollBarThickness() - 2;
    const int gap = style::margin;
    const int cols = jmax (1, (w + gap) / (340 + gap));
    const int colW = (w - (cols - 1) * gap) / cols;
    std::vector<int> colY ((size_t) cols, 0);
    for (auto& b : blocks)
    {
        if (b->wide)
        {
            const int y = *std::max_element (colY.begin(), colY.end());
            const int h = b->heightFor (w);
            b->setBounds (0, y, w, h);
            std::fill (colY.begin(), colY.end(), y + h + gap);
            continue;
        }
        const auto c = (size_t) (std::min_element (colY.begin(), colY.end()) - colY.begin());
        const int h = b->heightFor (colW);
        b->setBounds ((int) c * (colW + gap), colY[c], colW, h);
        colY[c] += h + gap;
    }
    content.setSize (w, jmax (1, *std::max_element (colY.begin(), colY.end())));
    for (auto& b : blocks) b->refresh();
}
} // namespace chroma::ui
