#include "PresetNormalizer.h"
#include "Loudness.h"
#include "PluginProcessor.h"
#include "ParamIDs.h"

namespace chroma
{
namespace
{
bool sameValue (const juce::var& a, const juce::var& b)
{
    const auto sa = a.toString(), sb = b.toString();
    if (sa == sb) return true;
    if (sa.containsChar (' ') || sb.containsChar (' ') || sa.isEmpty() || sb.isEmpty()) return false;
    if (! sa.containsOnly ("0123456789.-+eE") || ! sb.containsOnly ("0123456789.-+eE")) return false;
    const double x = sa.getDoubleValue(), y = sb.getDoubleValue();
    return std::abs (x - y) <= 1e-6 * juce::jmax (1.0, std::abs (x));
}

juce::ValueTree matchingChild (const juce::ValueTree& parent, const juce::ValueTree& c)
{
    for (auto d : parent)
        if (d.getType() == c.getType() && d.getProperty ("slot") == c.getProperty ("slot")) return d;
    return {};
}

// Strips layer-store values equal to the defaults (keeps the properties listed in keep).
void compactStore (juce::ValueTree& t, const juce::ValueTree& def, const juce::StringArray& keep)
{
    for (int i = t.getNumProperties(); --i >= 0;)
    {
        const auto name = t.getPropertyName (i);
        if (keep.contains (name.toString())) continue;
        if (def.hasProperty (name) && sameValue (t.getProperty (name), def.getProperty (name)))
            t.removeProperty (name, nullptr);
    }
    for (int i = t.getNumChildren(); --i >= 0;)
    {
        const auto c = t.getChild (i);
        const auto d = matchingChild (def, c);
        if (! d.isValid()) continue;
        bool same = c.getNumProperties() == d.getNumProperties();
        for (int k = 0; same && k < c.getNumProperties(); ++k)
            same = sameValue (c.getProperty (c.getPropertyName (k)), d.getProperty (c.getPropertyName (k)));
        if (same) t.removeChild (i, nullptr);
    }
}

juce::ValueTree layerChild (juce::ValueTree& content)
{
    auto e = content.getChildWithName ("ENGINE");
    if (! e.isValid())
    {
        e = juce::ValueTree ("ENGINE");
        content.appendChild (e, nullptr);
    }
    return e;
}
} // namespace

std::vector<std::vector<float>> PresetNormalizer::render (PresetKind kind, const juce::ValueTree& content, float* limiterMinGain)
{
    ChromaProcessor proc;
    proc.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    proc.setNonRealtime (true);
    if (kind == PresetKind::Multi)
        proc.restoreState (content, true);
    else
        proc.getSynth().pasteLayer (0, content);
    proc.prepareToPlay (sampleRate, blockSize);
    proc.getSynth().takeLimiterMinGain();

    const int total = (int) std::lround (seconds * sampleRate);
    std::vector<std::vector<float>> out (2, std::vector<float> ((size_t) total));
    juce::AudioBuffer<float> buf (2, blockSize);
    for (int pos = 0; pos < total; pos += blockSize)
    {
        const int n = juce::jmin (blockSize, total - pos);
        buf.setSize (2, n, false, false, true);
        buf.clear();
        juce::MidiBuffer midi;
        if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) velocity), 0);
        proc.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            std::copy (buf.getReadPointer (ch), buf.getReadPointer (ch) + n, out[(size_t) ch].begin() + pos);
    }
    if (limiterMinGain != nullptr) *limiterMinGain = proc.getSynth().takeLimiterMinGain();
    proc.releaseResources();
    return out;
}

PresetNormalizer::Measure PresetNormalizer::measure (PresetKind kind, const juce::ValueTree& content)
{
    float g = 1.0f;
    const auto audio = render (kind, content, &g);
    return { loudness::shortTermMax (audio, sampleRate), loudness::truePeakDb (audio), 20.0 * std::log10 (juce::jmax (1e-6f, g)) };
}

float PresetNormalizer::getTrim (PresetKind kind, const juce::ValueTree& content)
{
    if (kind == PresetKind::Multi)
        return (float) content.getChildWithProperty ("id", ParamIDs::presetTrim).getProperty ("value", 0.0f);
    return (float) content.getChildWithName ("ENGINE").getProperty (lp::descs[(size_t) lp::layerTrim].id, 0.0f);
}

void PresetNormalizer::setTrim (PresetKind kind, juce::ValueTree& content, float db)
{
    db = juce::jlimit (-24.0f, 24.0f, std::round (db * 100.0f) / 100.0f);
    if (kind == PresetKind::Multi)
    {
        auto p = content.getChildWithProperty ("id", ParamIDs::presetTrim);
        if (! p.isValid())
        {
            p = juce::ValueTree ("PARAM");
            p.setProperty ("id", ParamIDs::presetTrim, nullptr);
            content.appendChild (p, nullptr);
        }
        p.setProperty ("value", db, nullptr);
    }
    else
        layerChild (content).setProperty (lp::descs[(size_t) lp::layerTrim].id, db, nullptr);
}

juce::ValueTree PresetNormalizer::normalize (PresetKind kind, const juce::ValueTree& content, Result* res)
{
    Result r;
    auto c = content.createCopy();
    float trim = getTrim (kind, c);
    auto m = measure (kind, c);
    r.before = m;
    if (m.lufs < -90.0)
    {
        r.silent = true;
        r.after = m;
        r.trimDb = trim;
        if (res != nullptr) *res = r;
        return c;
    }
    for (int it = 0; it < 8; ++it)
    {
        double gain = targetLufs - m.lufs;
        // Peaks must stay under the limiter threshold (−1.7 dBTP, margin 0.1 dB): when the limiter
        // acted, back off by its gain reduction.
        const double headroom = m.limiterDb < -0.01 ? m.limiterDb - peakMargin
                                                    : (TruePeakLimiter::thresholdDb() - peakMargin) - m.truePeak;
        r.peakLimited = headroom < gain;
        gain = juce::jmin (gain, headroom);
        if (std::abs (gain) < 0.05) break;
        const float next = juce::jlimit (-24.0f, 24.0f, trim + (float) gain);
        if (std::abs (next - trim) < 0.01f) break;
        trim = next;
        setTrim (kind, c, trim);
        m = measure (kind, c);
        ++r.iterations;
    }
    r.after = m;
    r.trimDb = getTrim (kind, c);
    if (res != nullptr) *res = r;
    return c;
}

juce::ValueTree PresetNormalizer::compact (PresetKind kind, const juce::ValueTree& content)
{
    auto c = content.createCopy();
    const auto defStore = LayerParamStore().toTree ("LAYER");
    if (kind == PresetKind::Layer)
    {
        auto e = c.getChildWithName ("ENGINE");
        if (e.isValid()) compactStore (e, defStore, { lp::descs[(size_t) lp::layerTrim].id, "mmver" });
        return c;
    }
    ChromaProcessor ref;
    const auto def = ref.captureState (false);
    for (int i = c.getNumChildren(); --i >= 0;)
    {
        const auto p = c.getChild (i);
        if (! p.hasType ("PARAM") || p.getProperty ("id").toString() == ParamIDs::presetTrim) continue;
        const auto d = def.getChildWithProperty ("id", p.getProperty ("id"));
        if (d.isValid() && sameValue (p.getProperty ("value"), d.getProperty ("value"))) c.removeChild (i, nullptr);
    }
    auto core = c.getChildWithName ("CORE");
    for (int i = core.getNumChildren(); --i >= 0;)
    {
        auto l = core.getChild (i);
        if (! l.hasType ("LAYER")) continue;
        compactStore (l, defStore, { "index", "mmver" });   // mmver: Mod Matrix format, must survive compaction
        if (l.getNumProperties() <= 2 && l.getNumChildren() == 0) core.removeChild (i, nullptr);
    }
    return c;
}

bool PresetNormalizer::normalizeFile (const juce::File& f, Result* res)
{
    PresetInfo info;
    auto content = PresetManager::load (f, &info);
    if (! content) return false;
    const auto out = compact (info.kind, normalize (info.kind, *content, res));
    return PresetManager::writeFile (f, info.kind, out, info.meta);
}
} // namespace chroma
