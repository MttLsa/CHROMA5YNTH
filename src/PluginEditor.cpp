#include "PluginEditor.h"
#include "GuiLayout.h"
#include "engines/fm/FmSysex.h"

namespace chroma
{
namespace
{
constexpr int toolbarH = 32, layerBarH = 58, tabH = 26, rightW = 300;

juce::StringArray layerSections (const juce::String& prefix)
{
    juce::StringArray out;
    for (const auto& d : lp::descs)
        if (utf8 (d.section).startsWith (prefix)) out.addIfNotAlreadyThere (utf8 (d.section));
    return out;
}

juce::String shortTitle (const juce::String& section)
{
    return section.fromLastOccurrenceOf (utf8 (" · "), false, false);
}
} // namespace

juce::String ChromaEditor::aboutText()
{
    return juce::String (CHROMA_PRODUCT_NAME) + " v" + CHROMA_VERSION_STRING + "\n" + CHROMA_COMPANY_NAME;
}

ChromaEditor::ChromaEditor (ChromaProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);
    addAndMakeVisible (root);

    // ---- header + toolbar
    auto tool = [this] (juce::Button& b, const juce::String& tip, std::function<void()> fn)
    {
        b.setTooltip (tip);
        b.onClick = std::move (fn);
        root.addAndMakeVisible (b);
    };
    tool (aboutButton, "Informazioni su CHROMA5YNTH.", [this]
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "About", aboutText(), {}, this);
    });
    tool (zoomButton, utf8 ("Dimensione dell'interfaccia (75–200%). Trascina l'angolo per ridimensionare liberamente."), [this] { showZoomMenu(); });

    browser = std::make_unique<PresetBrowser> (p);
    browser->onClose = [this] { browser->setVisible (false); presetsButton.setToggleState (false, juce::dontSendNotification); };
    browser->onLoaded = [this] { updateToolbar(); };
    root.addChildComponent (*browser);
    presetsButton.setClickingTogglesState (true);
    tool (presetsButton, "Apri il browser dei preset (Multi e Layer).", [this]
    {
        if (presetsButton.getToggleState()) browser->refresh();
        browser->setVisible (presetsButton.getToggleState());
        browser->toFront (false);
    });
    presetName.setFont (style::font (13.0f, true));
    presetName.setColour (juce::Label::textColourId, style::text);
    presetName.setColour (juce::Label::backgroundColourId, style::outline);
    presetName.setTooltip ("Preset corrente.");
    root.addAndMakeVisible (presetName);
    tool (undoButton, "Annulla l'ultima modifica.", [this] { proc.undo(); });
    tool (redoButton, "Ripristina la modifica annullata.", [this] { proc.redo(); });
    for (auto* b : { &aButton, &bButton }) { b->setRadioGroupId (2); b->setClickingTogglesState (true); }
    aButton.setToggleState (true, juce::dontSendNotification);
    tool (aButton, "Confronto A/B: stato A.", [this] { proc.selectAB (0); });
    tool (bButton, "Confronto A/B: stato B (alla prima apertura copia di A).", [this] { proc.selectAB (1); });
    tool (copyAbButton, "Copia lo stato corrente nell'altro slot A/B.", [this] { proc.getHistory().copyActiveToOther(); });
    tool (randomButton, utf8 ("Randomizza: globale, Layer o sezione, con la quantità indicata dallo slider."), [this] { showRandomMenu(); });
    randomAmount.setRange (0.0, 1.0, 0.01);
    randomAmount.setValue (0.5, juce::dontSendNotification);
    randomAmount.setTooltip (utf8 ("Quantità di randomizzazione (0–100%)."));
    root.addAndMakeVisible (randomAmount);
    tool (initButton, "Riporta il synth selezionato ai valori iniziali (gli altri 11 synth non cambiano).", [this] { proc.initLayer (ctx.layer); });
    tool (tuningButton, "Microtuning: carica un file .scl (con .kbm opzionale) o .tun.", [this] { showTuningMenu(); });
    tool (midiButton, "Mappature MIDI learn: salva, carica, cancella.", [this] { showMidiMenu(); });

    // ---- layer bar
    for (int l = 0; l < numLayers; ++l)
    {
        tiles[(size_t) l] = std::make_unique<ui::LayerTile> (ctx, l);
        tiles[(size_t) l]->onSelect = [this] (int layer) { selectLayer (layer); };
        tiles[(size_t) l]->onMenu = [this] (int layer, juce::Component& c) { showLayerMenu (layer, c); };
        root.addAndMakeVisible (*tiles[(size_t) l]);
    }

    // ---- centre: tabs + pages
    static const char* const tabNames[] { "Engine", "Voice", "Modulation", "Layer FX", "Master FX", "Global" };
    static const char* const tabTips[] { "Moduli dell'engine del synth selezionato, nell'ordine del segnale (filtro in fondo).", "Synth, voce, performance, drift e inviluppo d'ampiezza.",
                                         "Inviluppi di modulazione, LFO e Mod Matrix.", "Effetti del Layer selezionato.",
                                         "Effetti sulla somma di tutti i Layer.", "Impostazioni globali e MPE." };
    for (int t = 0; t < numTabs; ++t)
    {
        auto& b = tabs[(size_t) t];
        b.setButtonText (tabNames[t]);
        b.setTooltip (tabTips[t]);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (77);
        b.setColour (juce::TextButton::buttonOnColourId, style::accent);   // page tabs: blue
        b.onClick = [this, t] { selectTab (t); };
        root.addAndMakeVisible (b);
        root.addChildComponent (pages[(size_t) t]);
    }
    buildStaticPages();

    // ---- right column: Master, Macro, Scale
    masterPanel = std::make_unique<ui::SectionPanel> (ctx, "Master", style::accentG);
    auto meter = std::make_unique<ui::StereoMeter>();
    stereoMeter = meter.get();
    masterPanel->setDisplay (std::move (meter), 46);
    masterPanel->addHostParam (ParamIDs::masterVolume, "Volume");
    masterPanel->addHostParam (ParamIDs::masterPolyphony, "Polyphony");
    root.addAndMakeVisible (*masterPanel);

    macroPanel = std::make_unique<ui::SectionPanel> (ctx, "Macro", style::accentG);
    for (int m = 1; m <= numMacros; ++m)
        macroPanel->addHostParam ("macro" + juce::String (m), "Macro " + juce::String (m));
    root.addAndMakeVisible (*macroPanel);

    scalePanel = std::make_unique<ui::SectionPanel> (ctx, "Note Assign", style::accent);
    scalePanel->setHeaderToggle (ctx.host (ParamIDs::scaleOn));
    auto kb = std::make_unique<ui::ScaleKeyboard> (ctx);
    keyboard = kb.get();
    scalePanel->setDisplay (std::move (kb), 78);
    root.addAndMakeVisible (*scalePanel);

    gscalePanel = ui::makeHostSection (ctx, "Global Scale", "Global Scale", style::accent);
    root.addAndMakeVisible (*gscalePanel);

    // ---- overlays
    wtEditor = std::make_unique<WavetableEditor> (p.getSynth());
    wtEditor->onClose = [this] { wtEditor->setVisible (false); };
    root.addChildComponent (*wtEditor);
    ctx.openWavetableEditor = [this] { wtEditor->setVisible (true); wtEditor->toFront (false); };

    selectTab (tabEngine);
    selectLayer (0);
    updateToolbar();

    // ---- size: 75–200 % of the base, maximum = the whole screen
    const auto saved = proc.editorSize;              // read first: setResizeLimits() already resizes
    int maxW = baseW * 2, maxH = baseH * 2;
    for (const auto& d : juce::Desktop::getInstance().getDisplays().displays)
    {
        maxW = juce::jmax (maxW, d.totalArea.getWidth());
        maxH = juce::jmax (maxH, d.totalArea.getHeight());
    }
    setResizable (true, true);
    setResizeLimits (juce::roundToInt (baseW * minScale), juce::roundToInt (baseH * minScale), maxW, maxH);
    if (saved.x >= baseW * minScale && saved.y >= baseH * minScale)
        setSize (juce::jmin (saved.x, maxW), juce::jmin (saved.y, maxH));
    else
        setSize (baseW, baseH);
}

ChromaEditor::~ChromaEditor()
{
    setLookAndFeel (nullptr);
}

void ChromaEditor::paint (juce::Graphics& g)
{
    g.fillAll (style::bg);
}

void ChromaEditor::resized()
{
    const float w = (float) getWidth(), h = (float) getHeight();
    scale = juce::jlimit (minScale, maxScale, juce::jmin (w / (float) baseW, h / (float) baseH));
    root.setTransform (juce::AffineTransform::scale (scale));
    root.setBounds (0, 0, (int) std::ceil (w / scale), (int) std::ceil (h / scale));
    zoomButton.setButtonText (juce::String (juce::roundToInt (scale * 100.0f)) + "%");
    proc.editorSize = { getWidth(), getHeight() };
}

void ChromaEditor::paintRoot (juce::Graphics& g)
{
    g.fillAll (style::bg);
    const auto bar = juce::Rectangle<float> ((float) style::margin, 6.0f, 520.0f, (float) style::titleH - 12.0f);
    drawHeader (g, bar, "multi-engine layered synth", {});
    g.setColour (style::textDim.withAlpha (0.6f));
    g.setFont (style::font (12.0f));
    g.drawText (juce::String ("v") + CHROMA_VERSION_STRING,
                juce::Rectangle<int> (zoomButton.getX() - 70, 0, 62, style::titleH), juce::Justification::centredRight);

    // centre title: selected layer + engine
    const auto& t = tabs[(size_t) numTabs - 1];
    const int engine = builtEngine >= 0 ? builtEngine : 0;
    g.setColour (style::text);
    g.setFont (style::font (12.0f, true));
    g.drawText ("LAYER " + juce::String (ctx.layer + 1) + utf8 ("  ·  ") + juce::String (ui::engineNames[(size_t) engine]).toUpperCase(),
                juce::Rectangle<int> (t.getRight() + 12, t.getY(), pages[0].getRight() - t.getRight() - 12, t.getHeight()),
                juce::Justification::centredRight);
}

void ChromaEditor::layoutRoot()
{
    const int W = root.getWidth(), H = root.getHeight(), m = style::margin;
    aboutButton.setBounds (W - m - 70, (style::titleH - 24) / 2, 70, 24);
    zoomButton.setBounds (aboutButton.getX() - 6 - 60, aboutButton.getY(), 60, 24);

    auto bar = juce::Rectangle<int> (m, style::titleH, W - 2 * m, toolbarH).reduced (0, 4);
    auto place = [&bar] (juce::Component& c, int w) { c.setBounds (bar.removeFromLeft (w)); bar.removeFromLeft (4); };
    place (presetsButton, 76);
    for (auto* c : std::initializer_list<juce::Component*> { &midiButton, &tuningButton, &initButton })
    {
        c->setBounds (bar.removeFromRight (64));
        bar.removeFromRight (4);
    }
    const int fixed = 2 * 56 + 3 * 34 + 90 + 110 + 8 * 4;
    place (presetName, juce::jmax (120, bar.getWidth() - fixed));
    place (undoButton, 56);
    place (redoButton, 56);
    place (aButton, 34);
    place (bButton, 34);
    place (copyAbButton, 34);
    place (randomButton, 90);
    place (randomAmount, juce::jmin (110, bar.getWidth()));

    // layer bar
    const int lbY = style::titleH + toolbarH;
    const float tw = (float) (W - 2 * m + 4) / (float) numLayers;
    for (int l = 0; l < numLayers; ++l)
        tiles[(size_t) l]->setBounds (m + juce::roundToInt (tw * (float) l), lbY, juce::roundToInt (tw) - 4, layerBarH - 6);

    // body
    auto body = juce::Rectangle<int> (m, lbY + layerBarH, W - 2 * m, H - lbY - layerBarH - m);
    const int rw = juce::jmax (rightW, juce::jmin (W / 4, 420));
    auto right = body.removeFromRight (rw);
    body.removeFromRight (m);
    auto tabRow = body.removeFromTop (tabH);
    for (auto& t : tabs) { t.setBounds (tabRow.removeFromLeft (96)); tabRow.removeFromLeft (3); }
    body.removeFromTop (6);
    for (auto& pg : pages) pg.setBounds (body);
    wtEditor->setBounds (body.getUnion (right));
    browser->setBounds (body.getUnion (right));

    for (auto* panel : { masterPanel.get(), macroPanel.get(), scalePanel.get(), gscalePanel.get() })
    {
        const int h = panel->heightFor (right.getWidth());
        panel->setBounds (right.removeFromTop (h));
        right.removeFromTop (m);
    }
    repaint();
}

//------------------------------------------------------------------------------ pages
void ChromaEditor::buildStaticPages()
{
    auto sec = [this] (const juce::String& s) { return ui::makeLayerSection (ctx, s, shortTitle (s)); };

    // Voice: synth -> voice -> performance, then drift and the amp envelope (the filter is in the engine page)
    auto& voice = pages[tabVoice];
    auto layerPanel = std::make_unique<ui::SectionPanel> (ctx, "Synth");
    for (const char* s : { "volume", "pan", "transpose", "key_lo", "key_hi", "vel_lo", "vel_hi", "midi_ch", "max_voices" })
        layerPanel->addHostOfLayer (s, {});
    voice.add (std::move (layerPanel));
    voice.add (sec (utf8 ("Layer · Voice")));
    voice.add (sec (utf8 ("Layer · Performance")));
    voice.newRow();
    voice.add (sec (utf8 ("Layer · Amp Env")))->weight = 2.0f;
    voice.add (sec (utf8 ("Layer · Drift")));

    // Modulation: sources (envelopes, LFOs) then the matrix. Unassigned Mod Env 2/3 and LFOs are greyed out.
    auto& mod = pages[tabMod];
    mod.newRow();
    for (int e = 1; e <= 3; ++e)
    {
        auto p = sec (utf8 ("Layer · Mod Env ") + juce::String (e));
        if (e == 1) p->setTitleNote ([] { return juce::String ("(Filter Envelope)"); });
        if (e == 2) p->setTitleNote ([this]
        {
            return builtEngine == 3 ? juce::String ("(Wavetable Filter 2 Envelope)") : juce::String();
        });
        if (e >= 2) p->setInUse ([this, e] { return ui::modSourceInUse (ctx, 5 + e); },
                                 utf8 ("Non assegnato: scegli Mod Env ") + juce::String (e) + utf8 (" come sorgente nella Mod Matrix per attivarlo."));
        mod.add (std::move (p));
    }
    mod.newRow();
    for (int l = 1; l <= 4; ++l)
    {
        auto p = sec (utf8 ("Layer · LFO ") + juce::String (l));
        p->setInUse ([this, l] { return ui::modSourceInUse (ctx, l); },
                     utf8 ("Non assegnato: scegli LFO ") + juce::String (l) + utf8 (" come sorgente nella Mod Matrix per attivarlo."));
        mod.add (std::move (p));
    }
    mod.newRow();
    mod.add (std::make_unique<ui::ModMatrixBlock> (ctx));

    // Layer FX (chain order)
    for (const auto& s : layerSections (utf8 ("Layer FX · ")))
        pages[tabFx].add (sec (s));

    // Master FX + Global (host)
    for (const auto* s : gui::sections)
    {
        const auto hs = utf8 (s);
        if (hs.startsWith (utf8 ("Master FX · ")))
            pages[tabMasterFx].add (ui::makeHostSection (ctx, hs, shortTitle (hs)));
        else if (hs == "Global" || hs == "MPE")
            pages[tabGlobal].add (ui::makeHostSection (ctx, hs, hs));
    }
}

void ChromaEditor::buildEnginePage (int engine)
{
    builtEngine = engine;
    fmTools = nullptr;
    if (engine != 3 && wtEditor != nullptr && wtEditor->isVisible())
        wtEditor->setVisible (false);               // windows of another engine close with the engine change
    auto& page = pages[tabEngine];
    page.clear();
    auto sec = [this] (const juce::String& s, float w = 1.0f)
    {
        auto p = ui::makeLayerSection (ctx, s, shortTitle (s));
        p->weight = w;
        return p;
    };
    auto add = [&page] (std::unique_ptr<ui::SectionPanel> p, float w) { p->weight = w; page.add (std::move (p)); };

    switch (juce::jlimit (0, 4, engine))
    {
        case 0:   // Analog: OSC 1-3 -> Mod / Sub / Noise -> Output -> Filter
            page.newRow();
            for (int o = 1; o <= 3; ++o) page.add (sec (utf8 ("Analog · OSC ") + juce::String (o)));
            page.newRow();
            page.add (sec (utf8 ("Analog · Mod")));
            page.add (sec (utf8 ("Analog · Sub")));
            page.add (sec (utf8 ("Analog · Noise")));
            page.newRow();
            page.add (sec (utf8 ("Analog · Output"), 1.0f));
            page.add (sec (utf8 ("Layer · Filter"), 3.0f));
            break;

        case 1:   // Additive: spectrum (+ editor) -> envelopes -> FM / AM -> stereo -> Filter
            page.newRow();
            page.add (sec (utf8 ("Additive · Spectrum")));
            page.newRow();
            page.add (std::make_unique<ui::AdditiveEditor> (ctx));
            page.newRow();
            page.add (sec (utf8 ("Additive · Partial Env")));
            page.add (sec (utf8 ("Additive · Spectral Env")));
            page.newRow();
            page.add (sec (utf8 ("Additive · FM")));
            page.add (sec (utf8 ("Additive · AM")));
            page.add (sec (utf8 ("Additive · Stereo")));
            page.newRow();
            page.add (sec (utf8 ("Layer · Filter")));
            break;

        case 2:   // FM6: algorithm -> each operator next to its envelope -> pitch env / LFO -> Filter
        {
            page.newRow();
            auto tools = std::make_unique<ui::ToolsBlock> (utf8 ("FM6 · Bank"), "Importa un banco di 32 voci (.syx) nel synth selezionato.");
            tools->addButton ("Import .syx", "Importa un banco .syx a 32 voci e scegli la voce da caricare.", [this] (juce::Component& c) { importFmBank (c); });
            fmTools = tools.get();
            page.add (sec (utf8 ("FM6 · Algorithm"), 2.0f));
            tools->weight = 1.0f;
            page.add (std::move (tools));
            for (int o = 1; o <= 6; ++o)
            {
                page.newRow();
                add (sec (utf8 ("FM6 · Op ") + juce::String (o)), 3.0f);
                add (sec (utf8 ("FM6 · Op ") + juce::String (o) + " Env"), 2.0f);
            }
            page.newRow();
            auto custom = sec (utf8 ("FM6 · Custom"));
            custom->shown = [this]   // only with the Custom algorithm (A or B)
            {
                const int c = (int) lp::descs[(size_t) lp::fmAlgorithm].numChoices - 1;
                return juce::roundToInt (ctx.store().get (lp::fmAlgorithm)) == c || juce::roundToInt (ctx.store().get (lp::fmAlgorithmB)) == c;
            };
            page.add (std::move (custom));
            page.newRow();
            page.add (sec (utf8 ("FM6 · Pitch Env")));
            page.add (sec (utf8 ("FM6 · LFO")));
            page.newRow();
            page.add (sec (utf8 ("Layer · Filter")));
            break;
        }

        case 3:   // Wavetable: each OSC next to its advanced module -> Sub/Noise -> Filter 1 + Filter 2
        {
            page.newRow();
            auto tools = std::make_unique<ui::ToolsBlock> (utf8 ("Wavetable · User Tables"), utf8 ("Disegna, importa e modifica le tabelle User 1–3 del synth."));
            tools->addButton ("WT Editor", "Apri l'editor delle wavetable utente.", [this] (juce::Component&) { if (ctx.openWavetableEditor) ctx.openWavetableEditor(); });
            for (int o = 1; o <= 3; ++o)
            {
                page.newRow();
                add (sec (utf8 ("Wavetable · OSC ") + juce::String (o)), 3.0f);
                add (sec (utf8 ("Wavetable · OSC ") + juce::String (o) + " Advanced"), 2.0f);
            }
            page.newRow();
            page.add (sec (utf8 ("Wavetable · Sub/Noise"), 2.0f));
            page.add (std::move (tools));
            page.newRow();
            page.add (sec (utf8 ("Layer · Filter")));
            page.add (sec (utf8 ("Wavetable · Filter 2")));
            break;
        }

        default:  // PD: each line (oscillator -> distortion -> PD env) -> lines mix + curve -> Filter
            for (int l = 1; l <= 2; ++l)
            {
                page.newRow();
                page.add (sec (utf8 ("PD · Line ") + juce::String (l)));
                page.add (sec (utf8 ("PD · Line ") + juce::String (l) + " Distortion"));
                page.add (sec (utf8 ("PD · Line ") + juce::String (l) + " PD Env"));
            }
            page.newRow();
            page.add (sec (utf8 ("PD · Lines")));
            page.add (std::make_unique<ui::PdCurveEditor> (ctx))->weight = 2.0f;
            page.newRow();
            page.add (sec (utf8 ("Layer · Filter")));
            break;
    }
    page.resized();
    root.repaint();
}

void ChromaEditor::selectLayer (int l)
{
    ctx.layer = juce::jlimit (0, numLayers - 1, l);
    refreshControls();
    root.repaint();
}

void ChromaEditor::selectTab (int t)
{
    currentTab = t;
    for (int i = 0; i < numTabs; ++i)
    {
        tabs[(size_t) i].setToggleState (i == t, juce::dontSendNotification);
        pages[(size_t) i].setVisible (i == t);
    }
    pages[(size_t) t].refresh();
}

//------------------------------------------------------------------------------ frame loop
void ChromaEditor::onFrame()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = (now - lastFrameMs) * 0.001;
    if (dt < 0.012) return;                       // ≤ 60 fps even on high refresh displays
    lastFrameMs = now;
    const double step = juce::jmin (dt, 0.1);
    for (auto& t : tiles) t->updateMeter (step);
    stereoMeter->update (proc.getSynth(), step);
    ++frameCount;
    if ((frameCount & 1) == 0) refreshControls();  // controls follow automation at 30 Hz
    if (frameCount % 6 == 0) updateToolbar();
}

void ChromaEditor::refreshControls()
{
    for (auto& t : tiles) t->refresh();
    const int engine = juce::roundToInt (ctx.layerHostParam (ctx.layer, "engine")->convertFrom0to1 (
                                             ctx.layerHostParam (ctx.layer, "engine")->getValue()));
    if (engine != builtEngine) buildEnginePage (engine);
    if (builtLayer != ctx.layer) { builtLayer = ctx.layer; root.repaint(); }
    pages[(size_t) currentTab].refresh();
    masterPanel->refresh();
    macroPanel->refresh();
    scalePanel->refresh();
    gscalePanel->refresh();
    keyboard->refresh();
}

void ChromaEditor::updateToolbar()
{
    presetName.setText ("  " + proc.getCurrentPresetName() + (proc.getSynth().getTuningName().isNotEmpty()
                                                              ? "   |   " + proc.getSynth().getTuningName() : juce::String()),
                        juce::dontSendNotification);
    redoButton.setEnabled (proc.getHistory().canRedo());
    aButton.setToggleState (proc.getHistory().activeSlot() == 0, juce::dontSendNotification);
    bButton.setToggleState (proc.getHistory().activeSlot() == 1, juce::dontSendNotification);
    midiButton.setButtonText (proc.getSynth().isMidiLearning() ? "Learn..." : "MIDI");
}

//------------------------------------------------------------------------------ menus
void ChromaEditor::showZoomMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Dimensione interfaccia");
    for (int pct : { 75, 100, 125, 150, 175, 200 })
        m.addItem (juce::String (pct) + "%", true, juce::roundToInt (scale * 100.0f) == pct, [this, pct]
        {
            const auto* c = getConstrainer();
            setSize (juce::jmin (c->getMaximumWidth(), baseW * pct / 100), juce::jmin (c->getMaximumHeight(), baseH * pct / 100));
        });
    m.addSeparator();
    m.addItem ("Schermo intero", [this]
    {
        if (const auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
            setSize (d->userArea.getWidth(), d->userArea.getHeight());
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&zoomButton));
}

void ChromaEditor::showLayerMenu (int layer, juce::Component& target)
{
    selectLayer (layer);
    auto& s = proc.getSynth();
    juce::PopupMenu m;
    m.addSectionHeader ("Layer " + juce::String (layer + 1));
    m.addItem ("Copia Layer", [this, layer] { layerClipboard = proc.getSynth().copyLayer (layer); });
    m.addItem ("Incolla Layer", layerClipboard.isValid(), false, [this, layer] { proc.getSynth().pasteLayer (layer, layerClipboard); });
    m.addItem ("Inizializza Layer", [&s, layer] { s.initLayer (layer); });
    m.addSeparator();
    m.addItem ("Salva preset Layer...", [this, layer]
    {
        chooser = std::make_unique<juce::FileChooser> ("Layer preset", proc.getPresets().getRoot(), "*.c5layer");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, layer] (const juce::FileChooser& fc)
        {
            if (fc.getResult() != juce::File()) proc.getSynth().saveLayerPreset (layer, fc.getResult().withFileExtension ("c5layer"));
        });
    });
    m.addItem ("Carica preset Layer...", [this, layer]
    {
        chooser = std::make_unique<juce::FileChooser> ("Layer preset", proc.getPresets().getRoot(), "*.c5layer");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, layer] (const juce::FileChooser& fc)
        {
            if (fc.getResult().existsAsFile()) proc.loadLayerPreset (layer, fc.getResult());
        });
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
}

void ChromaEditor::importFmBank (juce::Component& target)
{
    chooser = std::make_unique<juce::FileChooser> ("FM bank", juce::File(), "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, safe = juce::Component::SafePointer<juce::Component> (&target)] (const juce::FileChooser& fc)
    {
        juce::MemoryBlock data;
        if (! fc.getResult().existsAsFile() || ! fc.getResult().loadFileAsData (data)) return;
        auto bank = std::make_shared<std::vector<FmPatch>>();
        if (! parseFmBank (static_cast<const uint8_t*> (data.getData()), data.getSize(), *bank))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "FM6", "File .syx non valido (serve un banco a 32 voci).", {}, this);
            return;
        }
        juce::PopupMenu m;
        m.addSectionHeader (fc.getResult().getFileName());
        for (size_t i = 0; i < bank->size(); ++i)
            m.addItem (juce::String (i + 1) + "  " + juce::String ((*bank)[i].name).trim(), [this, bank, i]
            {
                auto* e = ctx.layerHostParam (ctx.layer, "engine");
                e->beginChangeGesture();
                e->setValueNotifyingHost (e->convertTo0to1 (2.0f));
                e->endChangeGesture();
                auto& st = ctx.store();
                for (const auto& [id, v] : (*bank)[i].values) st.set (id, v);
                if (fmTools != nullptr) fmTools->setInfo ("Caricata: " + juce::String ((*bank)[i].name).trim());
            });
        m.showMenuAsync (safe != nullptr ? juce::PopupMenu::Options().withTargetComponent (safe.getComponent())
                                         : juce::PopupMenu::Options().withTargetComponent (&root));
    });
}

void ChromaEditor::showRandomMenu()
{
    const float amt = (float) randomAmount.getValue();
    juce::PopupMenu m, layers, sections;
    m.addItem ("Randomizza tutto (Layer attivi + Master FX)", [this, amt] { proc.randomize ({ RandomizeScope::Global }, amt); });
    for (int l = 0; l < numLayers; ++l)
        layers.addItem ("Layer " + juce::String (l + 1), [this, amt, l] { proc.randomize ({ RandomizeScope::Layer, l }, amt); });
    m.addSubMenu ("Randomizza Layer", layers);
    for (int l = 0; l < numLayers; ++l)
    {
        juce::PopupMenu sub;
        for (const auto& sec : Randomizer::layerSections())
            sub.addItem (sec, [this, amt, l, sec] { proc.randomize ({ RandomizeScope::Section, l, sec }, amt); });
        sections.addSubMenu ("Layer " + juce::String (l + 1), sub);
    }
    for (const auto& sec : Randomizer::hostSections())
        sections.addItem (sec, [this, amt, sec] { proc.randomize ({ RandomizeScope::Section, 0, sec }, amt); });
    m.addSubMenu ("Randomizza sezione", sections);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&randomButton));
}

void ChromaEditor::showTuningMenu()
{
    juce::PopupMenu m;
    m.addItem ("Carica .scl / .tun...", [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Tuning", juce::File(), "*.scl;*.tun");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            juce::String err;
            if (f.existsAsFile() && ! proc.getSynth().loadTuningFile (f, &err))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Tuning", err, {}, this);
        });
    });
    m.addItem ("Accordatura standard (12-TET)", proc.getSynth().getTuningName().isNotEmpty(), false, [this] { proc.getSynth().resetTuning(); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&tuningButton));
}

void ChromaEditor::showMidiMenu()
{
    auto& s = proc.getSynth();
    juce::PopupMenu m;
    m.addItem ("Salva mappature CC...", [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("MIDI map", juce::File(), "*.c5midi");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult() != juce::File()) proc.getSynth().saveMidiMap (fc.getResult().withFileExtension ("c5midi"));
        });
    });
    m.addItem ("Carica mappature CC...", [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("MIDI map", juce::File(), "*.c5midi");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult().existsAsFile()) proc.getSynth().loadMidiMap (fc.getResult());
        });
    });
    m.addItem ("Cancella tutte le mappature (" + juce::String (s.getMidiMap().count) + ")", s.getMidiMap().count > 0, false, [&s] { s.clearMidiMap(); });
    if (s.isMidiLearning()) m.addItem ("Annulla MIDI Learn", [&s] { s.cancelMidiLearn(); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&midiButton));
}
} // namespace chroma
