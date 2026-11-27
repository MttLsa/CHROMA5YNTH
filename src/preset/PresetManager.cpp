#include "PresetManager.h"
#if CHROMA_HAS_FACTORY
 #include "FactoryPresets.h"
#endif

namespace chroma
{
namespace
{
PresetKind kindOf (const juce::File& f) { return f.hasFileExtension (PresetManager::layerExt) ? PresetKind::Layer : PresetKind::Multi; }

juce::String kindName (PresetKind k) { return k == PresetKind::Multi ? "multi" : "layer"; }

void writeMeta (juce::XmlElement& x, PresetKind k, const PresetMeta& m)
{
    x.setAttribute ("kind", kindName (k));
    x.setAttribute ("name", m.name);
    x.setAttribute ("category", m.category);
    x.setAttribute ("author", m.author);
    x.setAttribute ("tags", m.tags.joinIntoString (","));
    x.setAttribute ("format", 1);
}

bool insensitiveContains (const juce::StringArray& a, const juce::String& s)
{
    for (auto& x : a) if (x.equalsIgnoreCase (s)) return true;
    return false;
}
} // namespace

juce::File PresetManager::defaultRoot()
{
#if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
        .getChildFile ("Library/Audio/Presets/" CHROMA_COMPANY_NAME "/" CHROMA_PRODUCT_NAME);
#else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile (CHROMA_COMPANY_NAME "/" CHROMA_PRODUCT_NAME "/Presets");
#endif
}

juce::StringArray PresetManager::defaultCategories()
{
    return { "Arp", "Bass", "Bell", "Brass", "Chord", "Drum", "FX", "Keys", "Lead", "Organ", "Pad",
             "Percussive", "Pluck", "Sequence", "Strings", "Texture", "Other" };
}

PresetManager::PresetManager (juce::File r) : root (std::move (r))
{
    rescan();
}

juce::File PresetManager::folder (PresetKind k) const
{
    return root.getChildFile (k == PresetKind::Multi ? "Multi" : "Layer");
}

void PresetManager::rescan()
{
    presets.clear();
    favourites = juce::StringArray::fromLines (favouritesFile().loadFileAsString());
    favourites.removeEmptyStrings();
    for (auto k : { PresetKind::Multi, PresetKind::Layer })
    {
        const auto files = folder (k).findChildFiles (juce::File::findFiles, true, juce::String ("*") + extension (k));
        for (const auto& f : files)
        {
            PresetInfo info;
            if (! load (f, &info)) continue;
            info.favourite = favourites.contains (relative (f));
            presets.push_back (std::move (info));
        }
    }
    std::sort (presets.begin(), presets.end(), [] (const PresetInfo& a, const PresetInfo& b)
               { return a.meta.name.compareNatural (b.meta.name) < 0; });
}

std::vector<PresetInfo> PresetManager::filter (const Filter& f) const
{
    auto words = juce::StringArray::fromTokens (f.search, " ", "\"");
    words.removeEmptyStrings();
    std::vector<PresetInfo> out;
    for (const auto& p : presets)
    {
        if (p.kind != f.kind) continue;
        if (f.favouritesOnly && ! p.favourite) continue;
        if (f.category.isNotEmpty() && ! p.meta.category.equalsIgnoreCase (f.category)) continue;
        if (f.author.isNotEmpty() && ! p.meta.author.equalsIgnoreCase (f.author)) continue;
        if (f.tag.isNotEmpty() && ! insensitiveContains (p.meta.tags, f.tag)) continue;
        const auto hay = p.meta.name + " " + p.meta.category + " " + p.meta.author + " " + p.meta.tags.joinIntoString (" ");
        bool ok = true;
        for (auto& w : words) ok = ok && hay.containsIgnoreCase (w);
        if (ok) out.push_back (p);
    }
    return out;
}

juce::StringArray PresetManager::categories (PresetKind k) const
{
    auto c = defaultCategories();
    for (const auto& p : presets)
        if (p.kind == k && p.meta.category.isNotEmpty() && ! insensitiveContains (c, p.meta.category)) c.add (p.meta.category);
    return c;
}

juce::StringArray PresetManager::tags (PresetKind k) const
{
    juce::StringArray t;
    for (const auto& p : presets)
        if (p.kind == k)
            for (auto& x : p.meta.tags) if (! insensitiveContains (t, x)) t.add (x);
    t.sortNatural();
    return t;
}

juce::StringArray PresetManager::authors (PresetKind k) const
{
    juce::StringArray a;
    for (const auto& p : presets)
        if (p.kind == k && p.meta.author.isNotEmpty() && ! insensitiveContains (a, p.meta.author)) a.add (p.meta.author);
    a.sortNatural();
    return a;
}

juce::String PresetManager::safeFileName (const juce::String& name)
{
    auto s = juce::File::createLegalFileName (name.trim()).trim();
    return s.isEmpty() ? juce::String ("Untitled") : s;
}

bool PresetManager::writeFile (const juce::File& f, PresetKind k, const juce::ValueTree& content, const PresetMeta& meta)
{
    juce::XmlElement x (rootTag);
    writeMeta (x, k, meta);
    if (auto c = content.createXml()) x.addChildElement (c.release());
    f.getParentDirectory().createDirectory();
    return x.writeTo (f);
}

int PresetManager::numFactoryPresets()
{
#if CHROMA_HAS_FACTORY
    return ChromaFactory::namedResourceListSize;
#else
    return 0;
#endif
}

int PresetManager::installFactory (bool force)
{
    int written = 0;
#if CHROMA_HAS_FACTORY
    juce::String id;
    for (int i = 0; i < ChromaFactory::namedResourceListSize; ++i)
    {
        int size = 0;
        ChromaFactory::getNamedResource (ChromaFactory::namedResourceList[i], size);
        id << ChromaFactory::originalFilenames[i] << ":" << size << ";";
    }
    const auto marker = root.getChildFile (".factory");
    const auto stamp = juce::String::toHexString (id.hashCode64());
    if (! force && marker.loadFileAsString().trim() == stamp) return 0;
    // a new factory set replaces the old one: factory files that are no longer in the set are removed
    // (only inside the Factory folders; the user's own presets are never touched)
    juce::StringArray current;
    for (int i = 0; i < ChromaFactory::namedResourceListSize; ++i)
        current.add (juce::String::fromUTF8 (ChromaFactory::originalFilenames[i]));
    for (auto k : { PresetKind::Multi, PresetKind::Layer })
        for (const auto& f : folder (k).getChildFile ("Factory").findChildFiles (juce::File::findFiles, false, juce::String ("*") + extension (k)))
            if (! current.contains (f.getFileName())) f.deleteFile();
    for (int i = 0; i < ChromaFactory::namedResourceListSize; ++i)
    {
        int size = 0;
        const auto* data = ChromaFactory::getNamedResource (ChromaFactory::namedResourceList[i], size);
        const juce::String name = juce::String::fromUTF8 (ChromaFactory::originalFilenames[i]);
        const auto dir = folder (name.endsWithIgnoreCase (layerExt) ? PresetKind::Layer : PresetKind::Multi).getChildFile ("Factory");
        dir.createDirectory();
        if (data != nullptr && dir.getChildFile (name).replaceWithData (data, (size_t) size)) ++written;
    }
    root.createDirectory();
    marker.replaceWithText (stamp);
    rescan();
#else
    juce::ignoreUnused (force);
#endif
    return written;
}

juce::File PresetManager::uniqueFile (PresetKind k, const juce::String& name) const
{
    return folder (k).getNonexistentChildFile (safeFileName (name), extension (k), false);
}

juce::File PresetManager::save (PresetKind k, const juce::ValueTree& content, const PresetMeta& meta, bool overwrite)
{
    folder (k).createDirectory();
    auto f = folder (k).getChildFile (safeFileName (meta.name) + extension (k));
    if (f.exists() && ! overwrite) f = uniqueFile (k, meta.name);
    juce::XmlElement x (rootTag);
    writeMeta (x, k, meta);
    if (auto c = content.createXml()) x.addChildElement (c.release());
    if (! x.writeTo (f)) return {};
    rescan();
    return f;
}

std::optional<juce::ValueTree> PresetManager::load (const juce::File& f, PresetInfo* info)
{
    auto x = juce::XmlDocument::parse (f);
    if (x == nullptr) return std::nullopt;
    PresetInfo pi;
    pi.file = f;
    pi.kind = kindOf (f);
    pi.meta.name = f.getFileNameWithoutExtension();
    juce::ValueTree content;
    if (x->hasTagName (rootTag))
    {
        pi.kind = x->getStringAttribute ("kind") == "layer" ? PresetKind::Layer : PresetKind::Multi;
        pi.meta.name = x->getStringAttribute ("name", pi.meta.name);
        pi.meta.category = x->getStringAttribute ("category");
        pi.meta.author = x->getStringAttribute ("author");
        pi.meta.tags = juce::StringArray::fromTokens (x->getStringAttribute ("tags"), ",", "");
        pi.meta.tags.trim();
        pi.meta.tags.removeEmptyStrings();
        if (auto* c = x->getFirstChildElement()) content = juce::ValueTree::fromXml (*c);
    }
    else
    {
        content = juce::ValueTree::fromXml (*x);   // raw state (e.g. Synth::saveLayerPreset)
        pi.kind = content.hasType ("LAYER_PRESET") ? PresetKind::Layer : PresetKind::Multi;
    }
    if (! content.isValid()) return std::nullopt;
    if (info != nullptr) *info = std::move (pi);
    return content;
}

bool PresetManager::updateMeta (const juce::File& f, const PresetMeta& meta)
{
    PresetInfo info;
    auto content = load (f, &info);
    if (! content) return false;
    juce::XmlElement x (rootTag);
    writeMeta (x, info.kind, meta);
    if (auto c = content->createXml()) x.addChildElement (c.release());
    const bool ok = x.writeTo (f);
    rescan();
    return ok;
}

juce::File PresetManager::rename (const juce::File& f, const juce::String& newName)
{
    PresetInfo info;
    if (! load (f, &info) || newName.trim().isEmpty()) return {};
    const bool fav = isFavourite (f);
    auto dest = f.getSiblingFile (safeFileName (newName) + f.getFileExtension());
    if (dest != f && dest.exists()) dest = f.getParentDirectory().getNonexistentChildFile (safeFileName (newName), f.getFileExtension(), false);
    if (dest != f && ! f.moveFileTo (dest)) return {};
    info.meta.name = newName.trim();
    if (fav) { setFavourite (f, false); setFavourite (dest, true); }
    updateMeta (dest, info.meta);
    return dest;
}

bool PresetManager::remove (const juce::File& f)
{
    if (! f.isAChildOf (root)) return false;
    setFavourite (f, false);
    const bool ok = f.deleteFile();
    rescan();
    return ok;
}

juce::File PresetManager::importFile (const juce::File& src)
{
    PresetInfo info;
    auto content = load (src, &info);
    if (! content) return {};
    folder (info.kind).createDirectory();
    if (info.meta.name.isEmpty()) info.meta.name = src.getFileNameWithoutExtension();
    const auto dest = uniqueFile (info.kind, info.meta.name);
    juce::XmlElement x (rootTag);
    writeMeta (x, info.kind, info.meta);
    if (auto c = content->createXml()) x.addChildElement (c.release());
    if (! x.writeTo (dest)) return {};
    rescan();
    return dest;
}

bool PresetManager::exportFile (const juce::File& preset, const juce::File& destination) const
{
    return preset.existsAsFile() && preset.copyFileTo (destination);
}

void PresetManager::setFavourite (const juce::File& f, bool fav)
{
    const auto r = relative (f);
    favourites.removeString (r);
    if (fav) favourites.add (r);
    root.createDirectory();
    favouritesFile().replaceWithText (favourites.joinIntoString ("\n"));
    for (auto& p : presets) if (p.file == f) p.favourite = fav;
}

bool PresetManager::isFavourite (const juce::File& f) const
{
    return favourites.contains (relative (f));
}
} // namespace chroma
