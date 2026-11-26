#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <optional>
#include <vector>

namespace chroma
{
// §6 Preset library: Multi presets (full state) and Layer presets (one synth, loadable in any slot).
// Files are XML: <CHROMA5YNTH_PRESET kind name category author tags> + state tree child.
enum class PresetKind { Multi, Layer };

struct PresetMeta
{
    juce::String name, category, author;
    juce::StringArray tags;
};

struct PresetInfo
{
    juce::File file;
    PresetKind kind = PresetKind::Multi;
    PresetMeta meta;
    bool favourite = false;
};

class PresetManager
{
public:
    static constexpr const char* rootTag = "CHROMA5YNTH_PRESET";
    static constexpr const char* multiExt = ".c5multi";
    static constexpr const char* layerExt = ".c5layer";

    static juce::File defaultRoot();             // ~/Library/Audio/Presets/TMFU audiovibe/CHROMA5YNTH
    static juce::StringArray defaultCategories();
    static const char* extension (PresetKind k) { return k == PresetKind::Multi ? multiExt : layerExt; }

    explicit PresetManager (juce::File root = defaultRoot());

    const juce::File& getRoot() const noexcept { return root; }
    juce::File folder (PresetKind) const;

    void rescan();
    const std::vector<PresetInfo>& all() const noexcept { return presets; }

    struct Filter
    {
        PresetKind kind = PresetKind::Multi;
        juce::String search;                     // words matched against name/category/author/tags
        juce::String category, tag, author;      // empty = any
        bool favouritesOnly = false;
    };
    std::vector<PresetInfo> filter (const Filter&) const;
    juce::StringArray categories (PresetKind) const;   // defaults + those in use
    juce::StringArray tags (PresetKind) const;
    juce::StringArray authors (PresetKind) const;

    // Writes a preset (existing file with the same name is replaced when overwrite = true). Returns the file.
    juce::File save (PresetKind, const juce::ValueTree& content, const PresetMeta&, bool overwrite = true);
    // Reads a preset file (wrapped or raw state tree). Returns the content tree.
    static std::optional<juce::ValueTree> load (const juce::File&, PresetInfo* info = nullptr);

    juce::File rename (const juce::File&, const juce::String& newName);   // returns new file (invalid on failure)
    bool remove (const juce::File&);
    bool updateMeta (const juce::File&, const PresetMeta&);
    juce::File importFile (const juce::File& source);                     // copies into the library
    bool exportFile (const juce::File& preset, const juce::File& destination) const;

    void setFavourite (const juce::File&, bool);
    bool isFavourite (const juce::File&) const;

    static juce::String safeFileName (const juce::String&);
    // Writes a wrapped preset file (metadata + content).
    static bool writeFile (const juce::File&, PresetKind, const juce::ValueTree& content, const PresetMeta&);

    // Factory presets (presets/factory, embedded in the binary) are copied to <kind>/Factory once per
    // factory set (marker file .factory). Returns the number of files written.
    int installFactory (bool force = false);
    static int numFactoryPresets();

private:
    juce::File root;
    std::vector<PresetInfo> presets;
    juce::StringArray favourites;   // paths relative to root
    juce::File favouritesFile() const { return root.getChildFile ("favourites.txt"); }
    juce::String relative (const juce::File& f) const { return f.getRelativePathFrom (root).replaceCharacter ('\\', '/'); }
    juce::File uniqueFile (PresetKind, const juce::String& name) const;
};
} // namespace chroma
