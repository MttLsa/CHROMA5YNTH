// tools/presetnorm (§6): builds the factory presets from text recipes and normalises presets to
// −14 LUFS short-term max (C3, velocity 100, 4 s offline render), never above −1 dBTP.
//
//   presetnorm build <recipes dir> <out dir> [--report <file.md>]   recipes -> normalised, compact presets
//   presetnorm norm <preset files...>                                normalise files in place
//   presetnorm measure <preset files...>                             print loudness / true peak / trim
//
// Recipe syntax (presets/recipes/*.txt, UTF-8):
//   [Preset Name]                 starts a preset
//   kind = layer | multi          layer: one-layer patch, written as .c5layer and as a one-layer .c5multi
//   category = Pad / tags = a, b / author = ...
//   key = value                   layer recipes: layer keys (host layer suffix such as engine/volume/pan,
//                                 or any layer parameter id from params/layer_*.yaml)
//   L<n>.key = value              multi recipes: layer n (1..12); host parameters by id (scale_on, mfx_*, ...)
//   L<n> = @Other Preset          multi recipes: copies the lines of a layer recipe into layer n
//   macro<m> = L<n>.key min max   multi recipes: macro m (1..8) -> layer parameter, normalised range
//   scale.L1+L3 = C D E G A       multi recipes: SCALE slots of these notes play these layers (turns SCALE on;
//                                 slots not listed in any scale.* line are muted)
// Values: numbers in the parameter's units, choice labels (case-insensitive), On/Off for switches.
#include "PluginProcessor.h"
#include "ParamIDs.h"
#include "LayerParams.h"
#include "preset/PresetNormalizer.h"
#include "shared/ParamRange.h"
#include <juce_events/juce_events.h>
#include <cstdio>
#include <map>

using namespace chroma;

namespace
{
struct Line { juce::String key, value; int lineNo = 0; juce::String file; };
struct Recipe
{
    juce::String name, category, author { "TMFU audiovibe" };
    juce::StringArray tags;
    bool layerKind = false;
    std::vector<Line> lines;
};

[[noreturn]] void fail (const Line& l, const juce::String& msg)
{
    std::fprintf (stderr, "%s:%d: %s\n", l.file.toRawUTF8(), l.lineNo, msg.toRawUTF8());
    std::exit (2);
}

std::vector<Recipe> parseRecipes (const juce::File& dir)
{
    std::vector<Recipe> out;
    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.txt");
    files.sort();
    for (const auto& f : files)
    {
        juce::StringArray lines;
        f.readLines (lines);
        for (int i = 0; i < lines.size(); ++i)
        {
            auto s = lines[i].upToFirstOccurrenceOf ("#", false, false).trim();
            if (s.isEmpty()) continue;
            Line l { {}, {}, i + 1, f.getFileName() };
            if (s.startsWithChar ('[') && s.endsWithChar (']'))
            {
                out.push_back ({});
                out.back().name = s.substring (1, s.length() - 1).trim();
                continue;
            }
            if (out.empty() || ! s.containsChar ('=')) fail (l, "expected [Preset] or key = value");
            l.key = s.upToFirstOccurrenceOf ("=", false, false).trim();
            l.value = s.fromFirstOccurrenceOf ("=", false, false).trim();
            auto& r = out.back();
            if (l.key == "kind") r.layerKind = l.value == "layer";
            else if (l.key == "category") r.category = l.value;
            else if (l.key == "author") r.author = l.value;
            else if (l.key == "tags") { r.tags = juce::StringArray::fromTokens (l.value, ",", ""); r.tags.trim(); r.tags.removeEmptyStrings(); }
            else r.lines.push_back (l);
        }
    }
    return out;
}

int lpIndex (const juce::String& id)
{
    for (int i = 0; i < lp::count; ++i)
        if (id == lp::descs[(size_t) i].id) return i;
    return -1;
}

float parseLp (const Line& l, int id, const juce::String& v)
{
    const auto& d = lp::descs[(size_t) id];
    if (d.type == lp::Type::Choice)
    {
        for (int c = 0; c < d.numChoices; ++c)
            if (v.equalsIgnoreCase (juce::String::fromUTF8 (d.choices[c]))) return (float) c;
        if (v.containsOnly ("0123456789") && v.getIntValue() < d.numChoices) return (float) v.getIntValue();
        fail (l, "unknown choice '" + v + "' for " + d.id);
    }
    if (d.type == lp::Type::Bool)
    {
        if (v.equalsIgnoreCase ("on") || v == "1" || v.equalsIgnoreCase ("true")) return 1.0f;
        if (v.equalsIgnoreCase ("off") || v == "0" || v.equalsIgnoreCase ("false")) return 0.0f;
        fail (l, "expected On/Off for " + juce::String (d.id));
    }
    if (! v.containsOnly ("0123456789.-+eE")) fail (l, "expected a number for " + juce::String (d.id));
    const float x = v.getFloatValue();
    if (x < d.min - 1e-4f || x > d.max + 1e-4f) fail (l, "value out of range for " + juce::String (d.id));
    return x;
}

void setHost (const Line& l, ChromaProcessor& proc, const juce::String& id, const juce::String& v)
{
    auto* p = proc.getState().getParameter (id);
    if (p == nullptr) fail (l, "unknown parameter '" + id + "'");
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p))
    {
        for (int i = 0; i < c->choices.size(); ++i)
            if (c->choices[i].equalsIgnoreCase (v)) { p->setValueNotifyingHost (p->convertTo0to1 ((float) i)); return; }
        fail (l, "unknown choice '" + v + "' for " + id);
    }
    if (dynamic_cast<juce::AudioParameterBool*> (p) != nullptr)
    {
        const bool on = v.equalsIgnoreCase ("on") || v == "1" || v.equalsIgnoreCase ("true");
        if (! on && ! (v.equalsIgnoreCase ("off") || v == "0" || v.equalsIgnoreCase ("false"))) fail (l, "expected On/Off for " + id);
        p->setValueNotifyingHost (on ? 1.0f : 0.0f);
        return;
    }
    if (! v.containsOnly ("0123456789.-+eE")) fail (l, "expected a number for " + id);
    const auto range = p->getNormalisableRange();
    const float x = v.getFloatValue();
    if (x < range.start - 1e-4f || x > range.end + 1e-4f) fail (l, "value out of range for " + id);
    p->setValueNotifyingHost (p->convertTo0to1 (x));
}

void setLayerKey (const Line& l, ChromaProcessor& proc, int layer, const juce::String& key, const juce::String& v)
{
    const auto hostId = "layer" + juce::String (layer + 1) + "_" + key;
    if (proc.getState().getParameter (hostId) != nullptr) { setHost (l, proc, hostId, v); return; }
    const int id = lpIndex (key);
    if (id < 0) fail (l, "unknown layer key '" + key + "'");
    proc.getSynth().layerStore (layer).set (id, parseLp (l, id, v));
}

const Recipe* findRecipe (const std::vector<Recipe>& all, const juce::String& name)
{
    for (const auto& r : all) if (r.name == name) return &r;
    return nullptr;
}

void applyLayerRecipe (const Recipe& r, ChromaProcessor& proc, int layer)
{
    for (const auto& l : r.lines) setLayerKey (l, proc, layer, l.key, l.value);
}

void applyRecipe (const Recipe& r, const std::vector<Recipe>& all, ChromaProcessor& proc)
{
    if (r.layerKind) { applyLayerRecipe (r, proc, 0); return; }
    std::array<int, 12> masks {};
    bool scale = false;
    for (const auto& l : r.lines)
    {
        if (l.key.startsWith ("scale."))
        {
            int layersMask = 0;
            for (auto t : juce::StringArray::fromTokens (l.key.substring (6), "+", ""))
            {
                const int n = t.trim().substring (1).getIntValue();
                if (! t.trim().startsWithChar ('L') || n < 1 || n > numLayers) fail (l, "scale.L<n>+L<m> = notes");
                layersMask |= 1 << (n - 1);
            }
            static const char* sharps[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            static const char* flats[] { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
            for (auto t : juce::StringArray::fromTokens (l.value, " ", ""))
            {
                if (t.isEmpty()) continue;
                int k = 0;
                while (k < 12 && t != sharps[k] && t != flats[k]) ++k;
                if (k == 12) fail (l, "unknown note " + t);
                masks[(size_t) k] |= layersMask;
            }
            scale = true;
            continue;
        }
        if (l.key.startsWithIgnoreCase ("macro"))
        {
            const int m = l.key.substring (5).getIntValue() - 1;
            auto t = juce::StringArray::fromTokens (l.value, " ", "");
            t.removeEmptyStrings();
            if (m < 0 || m >= numMacros || t.size() != 3 || ! t[0].startsWithChar ('L') || ! t[0].containsChar ('.')) fail (l, "macro<m> = L<n>.key min max");
            const int layer = t[0].substring (1).upToFirstOccurrenceOf (".", false, false).getIntValue() - 1;
            const int id = lpIndex (t[0].fromFirstOccurrenceOf (".", false, false));
            if (layer < 0 || layer >= numLayers || id < 0) fail (l, "bad macro target " + t[0]);
            proc.getSynth().assignMacro (m, { layer, id }, t[1].getFloatValue(), t[2].getFloatValue());
            continue;
        }
        if (l.key.startsWithChar ('L') && l.key.substring (1, 2).containsOnly ("0123456789"))
        {
            const int layer = l.key.substring (1).getIntValue() - 1;
            if (layer < 0 || layer >= numLayers) fail (l, "layer out of range");
            if (! l.key.containsChar ('.'))
            {
                if (! l.value.startsWithChar ('@')) fail (l, "expected L<n> = @Layer Recipe");
                const auto* src = findRecipe (all, l.value.substring (1).trim());
                if (src == nullptr || ! src->layerKind) fail (l, "unknown layer recipe " + l.value);
                applyLayerRecipe (*src, proc, layer);
                continue;
            }
            setLayerKey (l, proc, layer, l.key.fromFirstOccurrenceOf (".", false, false), l.value);
            continue;
        }
        setHost (l, proc, l.key, l.value);
    }
    if (scale)
    {
        const Line l { "scale", "", 0, r.name };
        setHost (l, proc, ParamIDs::scaleOn, "On");
        for (int k = 0; k < 12; ++k)
            setHost (l, proc, ParamIDs::scaleSlot[(size_t) k], juce::String (masks[(size_t) k]));
    }
}

juce::String engineNames (const juce::ValueTree& multi)
{
    juce::StringArray e;
    for (int l = 0; l < numLayers; ++l)
    {
        const auto on = multi.getChildWithProperty ("id", ParamIDs::layerOn[(size_t) l]);
        const bool isOn = on.isValid() ? (bool) on.getProperty ("value") : l == 0;
        if (! isOn) continue;
        const auto eng = multi.getChildWithProperty ("id", ParamIDs::layerEngine[(size_t) l]);
        static const char* names[] { "Analog", "Additive", "FM6", "Wavetable", "PD" };
        e.addIfNotAlreadyThere (names[juce::jlimit (0, 4, eng.isValid() ? (int) eng.getProperty ("value") : 0)]);
    }
    return e.joinIntoString (" + ");
}

void printResult (const juce::String& name, const juce::String& kind, const PresetNormalizer::Result& r)
{
    std::printf ("%-28s %-5s  before %7.2f LUFS %6.2f dBTP  trim %+6.2f dB  after %7.2f LUFS %6.2f dBTP  lim %5.2f dB%s%s\n",
                 name.toRawUTF8(), kind.toRawUTF8(), r.before.lufs, r.before.truePeak, r.trimDb,
                 r.after.lufs, r.after.truePeak, r.after.limiterDb, r.peakLimited ? "  [peak-limited]" : "", r.silent ? "  [SILENT]" : "");
    std::fflush (stdout);
}

int build (const juce::File& recipesDir, const juce::File& outDir, const juce::File& report)
{
    const auto recipes = parseRecipes (recipesDir);
    std::map<juce::String, int> seen;
    for (const auto& r : recipes)
        if (seen[r.name]++ > 0) { std::fprintf (stderr, "duplicate preset name: %s\n", r.name.toRawUTF8()); return 2; }

    outDir.getChildFile ("Multi").deleteRecursively();
    outDir.getChildFile ("Layer").deleteRecursively();
    juce::StringArray md { "<!-- GENERATED by tools/presetnorm build - do not edit -->", "", "# Preset di fabbrica", "",
                           "Normalizzati con `tools/presetnorm`: nota C3, velocity 100, 4 s, render offline; trim per "
                           "−14 LUFS short-term max, mai sopra −1 dBTP.", "",
                           "| Preset | Categoria | Engine | Tag | LUFS (short-term max) | True peak | Trim |", "|---|---|---|---|---|---|---|" };
    int failures = 0;
    for (const auto& r : recipes)
    {
        const PresetMeta meta { r.name, r.category, r.author, r.tags };
        PresetNormalizer::Result res;
        juce::ValueTree multi;
        if (r.layerKind)
        {
            ChromaProcessor proc;
            applyRecipe (r, recipes, proc);
            const auto layer = PresetNormalizer::normalize (PresetKind::Layer, proc.getSynth().copyLayer (0), &res);
            printResult (r.name, "layer", res);
            failures += res.silent ? 1 : 0;
            PresetManager::writeFile (outDir.getChildFile ("Layer/" + PresetManager::safeFileName (r.name) + PresetManager::layerExt),
                                      PresetKind::Layer, PresetNormalizer::compact (PresetKind::Layer, layer), meta);
            ChromaProcessor host;
            host.getSynth().pasteLayer (0, layer);
            multi = host.captureState (false);
        }
        else
        {
            ChromaProcessor proc;
            applyRecipe (r, recipes, proc);
            multi = proc.captureState (false);
        }
        multi = PresetNormalizer::normalize (PresetKind::Multi, multi, &res);
        printResult (r.name, "multi", res);
        failures += res.silent ? 1 : 0;
        multi = PresetNormalizer::compact (PresetKind::Multi, multi);
        PresetManager::writeFile (outDir.getChildFile ("Multi/" + PresetManager::safeFileName (r.name) + PresetManager::multiExt),
                                  PresetKind::Multi, multi, meta);
        md.add ("| " + r.name + " | " + r.category + " | " + engineNames (multi) + " | " + r.tags.joinIntoString (", ") + " | "
                + juce::String (res.after.lufs, 1) + " | " + juce::String (res.after.truePeak, 1) + " dBTP | "
                + juce::String (res.trimDb, 2) + " dB |");
    }
    if (report != juce::File())
        report.replaceWithText (md.joinIntoString ("\n") + "\n");
    std::printf ("%d presets, %d silent\n", (int) recipes.size(), failures);
    return failures == 0 ? 0 : 1;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (juce::String::fromUTF8 (argv[i]));
    auto file = [] (const juce::String& p) { return juce::File::getCurrentWorkingDirectory().getChildFile (p); };

    if (args.size() >= 3 && args[0] == "build")
    {
        juce::File report;
        const int r = args.indexOf ("--report");
        if (r > 0 && r + 1 < args.size()) report = file (args[r + 1]);
        return build (file (args[1]), file (args[2]), report);
    }
    if (args.size() >= 2 && (args[0] == "norm" || args[0] == "measure"))
    {
        int bad = 0;
        for (int i = 1; i < args.size(); ++i)
        {
            const auto f = file (args[i]);
            PresetNormalizer::Result res;
            if (args[0] == "norm")
            {
                if (! PresetNormalizer::normalizeFile (f, &res)) { std::fprintf (stderr, "cannot read/write %s\n", args[i].toRawUTF8()); ++bad; continue; }
            }
            else
            {
                PresetInfo info;
                const auto content = PresetManager::load (f, &info);
                if (! content) { std::fprintf (stderr, "cannot read %s\n", args[i].toRawUTF8()); ++bad; continue; }
                res.before = res.after = PresetNormalizer::measure (info.kind, *content);
                res.trimDb = PresetNormalizer::getTrim (info.kind, *content);
            }
            printResult (f.getFileNameWithoutExtension(), f.getFileExtension().substring (3), res);
        }
        return bad == 0 ? 0 : 1;
    }
    std::fprintf (stderr, "usage:\n  presetnorm build <recipes dir> <out dir> [--report <file.md>]\n"
                          "  presetnorm norm <preset files...>\n  presetnorm measure <preset files...>\n");
    return 64;
}
