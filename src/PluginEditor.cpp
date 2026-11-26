#include "PluginEditor.h"
#include "GuiLayout.h"

namespace chroma
{
using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
constexpr int knobW = 76, knobH = 96, segW = 120, segH = 40, toolbarH = 32;
}

// Right click on any control -> MIDI learn menu (Italian, §5).
struct ChromaEditor::LearnMenu final : juce::MouseListener
{
    LearnMenu (ChromaEditor& e, juce::String id) : editor (e), paramId (std::move (id)) {}
    void mouseDown (const juce::MouseEvent& ev) override
    {
        if (ev.mods.isPopupMenu()) editor.showLearnMenu (paramId, *ev.eventComponent);
    }
    ChromaEditor& editor;
    juce::String paramId;
};

struct ChromaEditor::Control
{
    juce::String section;
    bool compact = false;                          // segmented selector instead of a knob
    std::unique_ptr<juce::Component> widget;
    std::unique_ptr<LearnMenu> learn;
};

juce::String ChromaEditor::aboutText()
{
    return juce::String (CHROMA_PRODUCT_NAME) + " v" + CHROMA_VERSION_STRING + "\n" + CHROMA_COMPANY_NAME;
}

ChromaEditor::ChromaEditor (ChromaProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lookAndFeel);
    auto& apvts = p.getState();

    for (const auto* s : gui::sections)
    {
        panels.push_back (std::make_unique<Panel> (s));
        content.addAndMakeVisible (*panels.back());
    }

    for (const auto& e : gui::params)
    {
        if (apvts.getParameter (e.id) == nullptr)
            continue;

        auto c = std::make_unique<Control>();
        c->section = e.section;
        const juce::String type (e.type);

        if (type == "choice" && e.numChoices > 4)
        {
            auto combo = std::make_unique<juce::ComboBox>();
            combo->addItemList (apvts.getParameter (e.id)->getAllValueStrings(), 1);
            combo->setTooltip (e.name);
            comboAttachments.push_back (std::make_unique<APVTS::ComboBoxAttachment> (apvts, e.id, *combo));
            c->widget = std::move (combo);
            c->compact = true;
        }
        else if (type == "choice" || type == "bool")
        {
            auto seg = std::make_unique<Segmented>();
            seg->attach (apvts, e.id);
            c->widget = std::move (seg);
            c->compact = true;
        }
        else
        {
            auto knob = std::make_unique<Knob> (e.name);
            knob->attach (apvts, e.id);
            c->widget = std::move (knob);
        }

        c->learn = std::make_unique<LearnMenu> (*this, e.id);
        c->widget->addMouseListener (c->learn.get(), true);
        content.addAndMakeVisible (*c->widget);
        controls.push_back (std::move (c));
    }

    aboutButton.onClick = [this]
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "About", aboutText(), {}, this);
    };
    addAndMakeVisible (aboutButton);

    wtEditor = std::make_unique<WavetableEditor> (p.getSynth());
    wtEditor->onClose = [this] { wtEditor->setVisible (false); wtButton.setToggleState (false, juce::dontSendNotification); };
    addChildComponent (*wtEditor);
    wtButton.setTooltip ("Editor delle wavetable utente (engine Wavetable).");
    wtButton.setClickingTogglesState (true);
    wtButton.onClick = [this] { wtEditor->setVisible (wtButton.getToggleState()); };
    addAndMakeVisible (wtButton);

    // §5/§6 toolbar
    auto tool = [this] (juce::Button& b, const juce::String& tip, std::function<void()> fn)
    {
        b.setTooltip (tip);
        b.onClick = std::move (fn);
        addAndMakeVisible (b);
    };
    browser = std::make_unique<PresetBrowser> (p);
    browser->onClose = [this] { browser->setVisible (false); presetsButton.setToggleState (false, juce::dontSendNotification); };
    browser->onLoaded = [this] { timerCallback(); };
    addChildComponent (*browser);
    presetsButton.setClickingTogglesState (true);
    tool (presetsButton, "Apri il browser dei preset (Multi e Layer).", [this]
    {
        if (presetsButton.getToggleState()) browser->refresh();
        browser->setVisible (presetsButton.getToggleState());
    });
    presetName.setFont (style::font (13.0f, true));
    presetName.setColour (juce::Label::textColourId, style::text);
    presetName.setTooltip ("Preset corrente.");
    addAndMakeVisible (presetName);
    tool (undoButton, "Annulla l'ultima modifica.", [this] { proc.undo(); });
    tool (redoButton, "Ripristina la modifica annullata.", [this] { proc.redo(); });
    for (auto* b : { &aButton, &bButton }) { b->setRadioGroupId (2); b->setClickingTogglesState (true); }
    aButton.setToggleState (true, juce::dontSendNotification);
    tool (aButton, "Confronto A/B: stato A.", [this] { proc.selectAB (0); });
    tool (bButton, "Confronto A/B: stato B (alla prima apertura copia di A).", [this] { proc.selectAB (1); });
    tool (copyAbButton, "Copia lo stato corrente nell'altro slot A/B.", [this] { proc.getHistory().copyActiveToOther(); });
    tool (randomButton, "Randomizza: globale, Layer o sezione, con la quantità indicata dallo slider.", [this] { showRandomMenu(); });
    randomAmount.setRange (0.0, 1.0, 0.01);
    randomAmount.setValue (0.5, juce::dontSendNotification);
    randomAmount.setTooltip ("Quantità di randomizzazione (0–100%).");
    addAndMakeVisible (randomAmount);
    tool (initButton, "Riporta l'intera patch ai valori iniziali (le impostazioni globali restano).", [this] { proc.initAll(); });
    tool (tuningButton, "Microtuning: carica un file .scl (con .kbm opzionale) o .tun.", [this] { showTuningMenu(); });
    tool (midiButton, "Mappature MIDI learn: salva, carica, cancella.", [this] { showMidiMenu(); });
    startTimerHz (10);

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    setResizable (true, true);
    setResizeLimits (480, 320, 2400, 1800);
    setSize (980, 720);
}

ChromaEditor::~ChromaEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ChromaEditor::paint (juce::Graphics& g)
{
    g.fillAll (style::bg);
    const auto bar = juce::Rectangle<float> ((float) style::margin, 6.0f, (float) (getWidth() - 2 * style::margin - 190),
                                             (float) style::titleH - 12.0f);
    drawHeader (g, bar, "multi-engine layered synth", juce::String ("v") + CHROMA_VERSION_STRING);
}

void ChromaEditor::resized()
{
    aboutButton.setBounds (getWidth() - style::margin - 80, (style::titleH - 24) / 2, 80, 24);
    wtButton.setBounds (aboutButton.getX() - style::margin - 90, aboutButton.getY(), 90, 24);

    auto bar = juce::Rectangle<int> (style::margin, style::titleH, getWidth() - 2 * style::margin, toolbarH).reduced (0, 4);
    auto place = [&bar] (juce::Component& c, int w) { c.setBounds (bar.removeFromLeft (w)); bar.removeFromLeft (style::margin / 2); };
    place (presetsButton, 70);
    for (auto* c : std::initializer_list<juce::Component*> { &midiButton, &tuningButton, &initButton })
    {
        c->setBounds (bar.removeFromRight (60));
        bar.removeFromRight (style::margin / 2);
    }
    const int nameW = juce::jmax (80, bar.getWidth() - (2 * 52 + 3 * 32 + 84 + 90 + 9 * style::margin / 2));
    place (presetName, nameW);
    place (undoButton, 52);
    place (redoButton, 52);
    place (aButton, 32);
    place (bButton, 32);
    place (copyAbButton, 32);
    place (randomButton, 84);
    place (randomAmount, juce::jmin (90, bar.getWidth()));

    const auto body = getLocalBounds().withTrimmedTop (style::titleH + toolbarH);
    wtEditor->setBounds (body);
    browser->setBounds (body);
    viewport.setBounds (body);
    const int w = viewport.getWidth() - viewport.getScrollBarThickness();
    content.setSize (w, layoutContent (w));
}

// Panels stacked vertically; controls wrap into rows inside each panel.
int ChromaEditor::layoutContent (int width)
{
    const int rowH = knobH + style::margin;
    const int innerW = width - 2 * style::margin - 12;
    int y = 0;
    for (size_t i = 0; i < panels.size(); ++i)
    {
        int x = 0, rows = 1;
        for (auto& c : controls)
        {
            if (c->section != gui::sections[i]) continue;
            const int cw = c->compact ? segW : knobW;
            if (x > 0 && x + cw > innerW) { x = 0; ++rows; }
            x += cw + style::margin;
        }
        const int h = style::panelHeaderH + rows * rowH + 8;
        auto& panel = *panels[i];
        panel.setBounds (style::margin, y, width - 2 * style::margin, h);
        const auto area = panel.content() + panel.getPosition();
        x = 0;
        int row = 0;
        for (auto& c : controls)
        {
            if (c->section != gui::sections[i]) continue;
            const int cw = c->compact ? segW : knobW;
            if (x > 0 && x + cw > area.getWidth()) { x = 0; ++row; }
            const juce::Rectangle<int> cell (area.getX() + x, area.getY() + row * rowH, cw, knobH);
            c->widget->setBounds (c->compact ? cell.withSizeKeepingCentre (segW, segH) : cell);
            x += cw + style::margin;
        }
        y += h + style::margin;
    }
    return y;
}
} // namespace chroma

namespace chroma
{
void ChromaEditor::timerCallback()
{
    presetName.setText (proc.getCurrentPresetName() + (proc.getSynth().getTuningName().isNotEmpty()
                                                       ? "   |   " + proc.getSynth().getTuningName() : juce::String()),
                        juce::dontSendNotification);
    undoButton.setEnabled (true);
    redoButton.setEnabled (proc.getHistory().canRedo());
    aButton.setToggleState (proc.getHistory().activeSlot() == 0, juce::dontSendNotification);
    bButton.setToggleState (proc.getHistory().activeSlot() == 1, juce::dontSendNotification);
    midiButton.setButtonText (proc.getSynth().isMidiLearning() ? "Learn..." : "MIDI");
}

void ChromaEditor::showLearnMenu (const juce::String& paramId, juce::Component& target)
{
    auto& s = proc.getSynth();
    const ParamTarget t { ParamTarget::host, s.hostParamIndex (paramId) };
    if (t.id < 0) return;
    const int cc = s.midiMappingFor (t);
    juce::PopupMenu m;
    m.addSectionHeader ("MIDI");
    m.addItem ("MIDI Learn (muovi un controller)", [&s, t] { s.startMidiLearn (t); });
    m.addItem (cc >= 0 ? "Rimuovi MIDI Learn (CC " + juce::String (cc) + ")" : juce::String ("Rimuovi MIDI Learn"), cc >= 0,
               false, [&s, t] { s.removeMidiMapping (t); });
    if (s.isMidiLearning()) m.addItem ("Annulla MIDI Learn", [&s] { s.cancelMidiLearn(); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
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
