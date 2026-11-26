#include "PresetBrowser.h"

namespace chroma
{
namespace
{
constexpr int rowH = 24, ctlH = 24;
const juce::String anyItem ("All");

void fillCombo (juce::ComboBox& c, const juce::StringArray& items, bool withAny)
{
    const auto current = c.getText();
    c.clear (juce::dontSendNotification);
    int id = 1;
    if (withAny) c.addItem (anyItem, id++);
    for (auto& s : items) c.addItem (s, id++);
    c.setText (current.isEmpty() && withAny ? anyItem : current, juce::dontSendNotification);
    if (withAny && c.getSelectedId() == 0) c.setSelectedId (1, juce::dontSendNotification);
}

juce::String comboFilter (const juce::ComboBox& c) { return c.getText() == anyItem ? juce::String() : c.getText(); }
} // namespace

PresetBrowser::PresetBrowser (ChromaProcessor& p) : proc (p)
{
    for (auto* b : { &multiMode, &layerMode })
    {
        b->setRadioGroupId (1);
        b->setClickingTogglesState (true);
        b->onClick = [this] { targetLayer.setVisible (kind() == PresetKind::Layer); refresh(); resized(); };
        addAndMakeVisible (*b);
    }
    multiMode.setToggleState (true, juce::dontSendNotification);
    multiMode.setTooltip ("Preset Multi: stato completo (tutti i Layer, Master, Macro).");
    layerMode.setTooltip ("Preset Layer: un singolo synth, caricabile in qualunque slot.");

    for (int l = 1; l <= numLayers; ++l) targetLayer.addItem ("Layer " + juce::String (l), l);
    targetLayer.setSelectedId (1, juce::dontSendNotification);
    targetLayer.setTooltip ("Layer di destinazione per Load / origine per Save.");
    addChildComponent (targetLayer);

    search.setTextToShowWhenEmpty ("Search", style::textDim);
    search.setTooltip ("Cerca per nome, categoria, autore o tag (tutte le parole).");
    search.onTextChange = [this] { refilter(); };
    addAndMakeVisible (search);
    for (auto* c : { &categoryFilter, &tagFilter, &authorFilter })
    {
        c->onChange = [this] { refilter(); };
        addAndMakeVisible (*c);
    }
    categoryFilter.setTooltip ("Filtra per categoria.");
    tagFilter.setTooltip ("Filtra per tag.");
    authorFilter.setTooltip ("Filtra per autore.");
    favFilter.setClickingTogglesState (true);
    favFilter.setTooltip ("Mostra solo i preferiti.");
    favFilter.onClick = [this] { refilter(); };
    addAndMakeVisible (favFilter);

    list.setRowHeight (rowH);
    list.setColour (juce::ListBox::backgroundColourId, style::panel);
    list.setColour (juce::ListBox::outlineColourId, style::outline);
    list.setOutlineThickness (1);
    addAndMakeVisible (list);

    name.setTextToShowWhenEmpty ("Name", style::textDim);
    author.setTextToShowWhenEmpty ("Author", style::textDim);
    tags.setTextToShowWhenEmpty ("Tags (comma separated)", style::textDim);
    name.setTooltip ("Nome del preset (Save / Rename).");
    author.setTooltip ("Autore del preset.");
    tags.setTooltip ("Tag separati da virgola.");
    category.setEditableText (true);
    category.setTooltip ("Categoria del preset (scegli o scrivi).");
    for (auto* c : std::initializer_list<juce::Component*> { &name, &author, &tags, &category }) addAndMakeVisible (*c);

    addButton (saveB, "Salva lo stato corrente come preset (sovrascrive un preset con lo stesso nome).", [this] { save(); });
    addButton (loadB, "Carica il preset selezionato (doppio clic sulla lista).", [this] { loadSelected(); });
    addButton (renameB, "Rinomina il preset selezionato con il nome indicato.", [this] { renameSelected(); });
    addButton (deleteB, "Elimina il preset selezionato.", [this] { deleteSelected(); });
    addButton (importB, "Importa un file preset nella libreria.", [this] { importPreset(); });
    addButton (exportB, "Esporta il preset selezionato in un file.", [this] { exportSelected(); });
    addButton (favB, "Aggiungi/rimuovi dai preferiti.", [this]
    {
        if (auto* s = selected()) { proc.getPresets().setFavourite (s->file, ! s->favourite); refilter(); }
    });
    addButton (closeB, "Chiudi il browser.", [this] { if (onClose) onClose(); });
    refresh();
}

PresetBrowser::~PresetBrowser() { list.setModel (nullptr); }

void PresetBrowser::addButton (juce::Button& b, const juce::String& tip, std::function<void()> fn)
{
    b.setTooltip (tip);
    b.onClick = std::move (fn);
    addAndMakeVisible (b);
}

void PresetBrowser::refresh()
{
    auto& pm = proc.getPresets();
    pm.rescan();
    fillCombo (categoryFilter, pm.categories (kind()), true);
    fillCombo (tagFilter, pm.tags (kind()), true);
    fillCombo (authorFilter, pm.authors (kind()), true);
    fillCombo (category, pm.categories (kind()), false);
    refilter();
}

void PresetBrowser::refilter()
{
    PresetManager::Filter f;
    f.kind = kind();
    f.search = search.getText();
    f.category = comboFilter (categoryFilter);
    f.tag = comboFilter (tagFilter);
    f.author = comboFilter (authorFilter);
    f.favouritesOnly = favFilter.getToggleState();
    const auto keep = selected() != nullptr ? selected()->file : juce::File();
    shown = proc.getPresets().filter (f);
    list.updateContent();
    list.deselectAllRows();
    for (size_t i = 0; i < shown.size(); ++i)
        if (shown[i].file == keep) list.selectRow ((int) i, false, true);
    list.repaint();
}

const PresetInfo* PresetBrowser::selected() const
{
    const int r = list.getSelectedRow();
    return r >= 0 && r < (int) shown.size() ? &shown[(size_t) r] : nullptr;
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool isSelected)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto& p = shown[(size_t) row];
    if (isSelected) g.fillAll (style::accent);
    const auto fg = isSelected ? juce::Colours::black : style::text;
    const auto dim = isSelected ? juce::Colours::black : style::textDim;
    g.setFont (style::font (12.0f, isSelected));
    g.setColour (p.favourite ? (isSelected ? juce::Colours::black : style::accent2) : dim);
    g.drawText (p.favourite ? juce::String::charToString ((juce::juce_wchar) 0x2605) : juce::String(), 4, 0, 16, h, juce::Justification::centred);
    g.setColour (fg);
    const int nameW = w * 2 / 5;
    g.drawText (p.meta.name, 24, 0, nameW - 28, h, juce::Justification::centredLeft, true);
    g.setColour (dim);
    g.setFont (style::font (11.0f));
    g.drawText (p.meta.category, nameW, 0, w / 6, h, juce::Justification::centredLeft, true);
    g.drawText (p.meta.author, nameW + w / 6, 0, w / 6, h, juce::Justification::centredLeft, true);
    g.drawText (p.meta.tags.joinIntoString (", "), nameW + w / 3, 0, w - nameW - w / 3 - 4, h, juce::Justification::centredLeft, true);
}

void PresetBrowser::selectedRowsChanged (int)
{
    if (auto* s = selected())
    {
        name.setText (s->meta.name, false);
        author.setText (s->meta.author, false);
        tags.setText (s->meta.tags.joinIntoString (", "), false);
        category.setText (s->meta.category, juce::dontSendNotification);
    }
}

void PresetBrowser::listBoxItemDoubleClicked (int, const juce::MouseEvent&) { loadSelected(); }

PresetMeta PresetBrowser::metaFromFields() const
{
    PresetMeta m;
    m.name = name.getText().trim();
    m.author = author.getText().trim();
    m.category = category.getText().trim();
    m.tags = juce::StringArray::fromTokens (tags.getText(), ",", "");
    m.tags.trim();
    m.tags.removeEmptyStrings();
    m.tags.removeDuplicates (true);
    return m;
}

void PresetBrowser::loadSelected()
{
    auto* s = selected();
    if (s == nullptr) return;
    const bool ok = kind() == PresetKind::Multi ? proc.loadMultiPreset (s->file)
                                                : proc.loadLayerPreset (targetLayer.getSelectedId() - 1, s->file);
    if (ok && onLoaded) onLoaded();
}

void PresetBrowser::save()
{
    auto m = metaFromFields();
    if (m.name.isEmpty()) m.name = kind() == PresetKind::Multi ? proc.getCurrentPresetName() : "Layer";
    if (kind() == PresetKind::Multi) proc.saveMultiPreset (m);
    else proc.saveLayerPreset (targetLayer.getSelectedId() - 1, m);
    refresh();
    for (size_t i = 0; i < shown.size(); ++i)
        if (shown[i].meta.name == m.name) list.selectRow ((int) i);
    if (onLoaded) onLoaded();
}

void PresetBrowser::renameSelected()
{
    auto* s = selected();
    if (s == nullptr || name.getText().trim().isEmpty()) return;
    auto& pm = proc.getPresets();
    const auto f = pm.rename (s->file, name.getText());
    if (f != juce::File())
    {
        auto m = metaFromFields();
        pm.updateMeta (f, m);
    }
    refresh();
}

void PresetBrowser::deleteSelected()
{
    auto* s = selected();
    if (s == nullptr) return;
    const auto file = s->file;
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Delete",
                                        "Eliminare il preset \"" + s->meta.name + "\"?", "Delete", "Cancel", this,
                                        juce::ModalCallbackFunction::create ([this, file] (int r)
                                        {
                                            if (r != 0) { proc.getPresets().remove (file); refresh(); }
                                        }));
}

void PresetBrowser::importPreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Import", juce::File(), juce::String ("*") + PresetManager::multiExt + ";*" + PresetManager::layerExt);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.existsAsFile()) { proc.getPresets().importFile (f); refresh(); }
    });
}

void PresetBrowser::exportSelected()
{
    auto* s = selected();
    if (s == nullptr) return;
    const auto src = s->file;
    chooser = std::make_unique<juce::FileChooser> ("Export", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (src.getFileName()),
                                                   juce::String ("*") + src.getFileExtension());
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, src] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f != juce::File()) proc.getPresets().exportFile (src, f.withFileExtension (src.getFileExtension()));
    });
}

void PresetBrowser::paint (juce::Graphics& g)
{
    g.fillAll (style::bg);
    auto r = getLocalBounds().reduced (style::margin);
    g.setColour (style::header);
    g.fillRoundedRectangle (r.removeFromTop (style::panelHeaderH).toFloat(), style::panelRadius);
    g.setColour (style::accent);
    g.fillRect (style::margin, style::margin, 3, style::panelHeaderH);
    g.setColour (style::text);
    g.setFont (style::font (12.0f, true));
    g.drawText ("PRESETS", style::margin + 10, style::margin, 200, style::panelHeaderH, juce::Justification::centredLeft);
}

void PresetBrowser::resized()
{
    auto r = getLocalBounds().reduced (style::margin);
    r.removeFromTop (style::panelHeaderH + style::margin);
    const int m = style::margin;

    auto top = r.removeFromTop (ctlH);
    multiMode.setBounds (top.removeFromLeft (64));
    layerMode.setBounds (top.removeFromLeft (64));
    top.removeFromLeft (m);
    if (targetLayer.isVisible()) { targetLayer.setBounds (top.removeFromLeft (100)); top.removeFromLeft (m); }
    favFilter.setBounds (top.removeFromRight (90));
    top.removeFromRight (m);
    const int cw = juce::jmin (130, top.getWidth() / 5);
    authorFilter.setBounds (top.removeFromRight (cw));
    top.removeFromRight (m);
    tagFilter.setBounds (top.removeFromRight (cw));
    top.removeFromRight (m);
    categoryFilter.setBounds (top.removeFromRight (cw));
    top.removeFromRight (m);
    search.setBounds (top);
    r.removeFromTop (m);

    auto buttons = r.removeFromBottom (ctlH);
    const int bw = juce::jmin (90, (buttons.getWidth() - 7 * m) / 8);
    for (auto* b : { &saveB, &loadB, &renameB, &deleteB, &importB, &exportB, &favB })
    {
        b->setBounds (buttons.removeFromLeft (bw));
        buttons.removeFromLeft (m);
    }
    closeB.setBounds (buttons.removeFromRight (bw));
    r.removeFromBottom (m);

    auto fields = r.removeFromBottom (ctlH);
    const int fw = (fields.getWidth() - 3 * m) / 4;
    name.setBounds (fields.removeFromLeft (fw));
    fields.removeFromLeft (m);
    category.setBounds (fields.removeFromLeft (fw));
    fields.removeFromLeft (m);
    author.setBounds (fields.removeFromLeft (fw));
    fields.removeFromLeft (m);
    tags.setBounds (fields);
    r.removeFromBottom (m);
    list.setBounds (r);
}
} // namespace chroma
