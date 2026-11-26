#pragma once
#include "PluginProcessor.h"
#include "StyleLookAndFeel.h"

namespace chroma
{
// §6 Preset browser: Multi / Layer presets, categories, tags, author, favourites, search,
// Save / Load / Rename / Delete / Import / Export. Overlay panel inside the editor.
class PresetBrowser final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit PresetBrowser (ChromaProcessor&);
    ~PresetBrowser() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();                                // rescan library and refilter

    std::function<void()> onClose, onLoaded;

private:
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int row) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    PresetKind kind() const { return layerMode.getToggleState() ? PresetKind::Layer : PresetKind::Multi; }
    const PresetInfo* selected() const;
    void refilter();
    void loadSelected();
    void save();
    void renameSelected();
    void deleteSelected();
    void importPreset();
    void exportSelected();
    PresetMeta metaFromFields() const;
    void addButton (juce::Button&, const juce::String& tip, std::function<void()>);

    ChromaProcessor& proc;
    std::vector<PresetInfo> shown;
    juce::TextButton multiMode { "Multi" }, layerMode { "Layer" }, favFilter { "Favourites" };
    juce::ComboBox targetLayer, categoryFilter, tagFilter, authorFilter, category;
    juce::TextEditor search, name, author, tags;
    juce::ListBox list { "presets", this };
    juce::TextButton saveB { "Save" }, loadB { "Load" }, renameB { "Rename" }, deleteB { "Delete" },
                     importB { "Import" }, exportB { "Export" }, favB { "Favourite" }, closeB { "Close" };
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBrowser)
};
} // namespace chroma
