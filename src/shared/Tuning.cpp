#include "Tuning.h"
#include <cmath>

namespace chroma::tuning
{
namespace
{
void setError (juce::String* e, const juce::String& msg) { if (e != nullptr) *e = msg; }

// Non-comment lines ('!' starts a comment line), trimmed.
juce::StringArray contentLines (const juce::String& text)
{
    juce::StringArray out;
    for (auto l : juce::StringArray::fromLines (text))
    {
        l = l.trim();
        if (! l.startsWithChar ('!')) out.add (l);
    }
    return out;
}

std::optional<double> parsePitch (juce::String s)
{
    s = s.trim().upToFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf ("\t", false, false);
    if (s.isEmpty()) return std::nullopt;
    if (s.containsChar ('.'))
        return s.getDoubleValue();
    const auto num = s.upToFirstOccurrenceOf ("/", false, false).getLargeIntValue();
    const auto den = s.containsChar ('/') ? s.fromFirstOccurrenceOf ("/", false, false).getLargeIntValue() : 1;
    if (num <= 0 || den <= 0) return std::nullopt;
    return 1200.0 * std::log2 ((double) num / (double) den);
}

double degreeCents (const Scale& sc, int degree)
{
    const int n = (int) sc.cents.size();
    const int oct = (int) std::floor ((double) degree / n);
    const int k = degree - oct * n;
    return oct * sc.cents.back() + (k == 0 ? 0.0 : sc.cents[(size_t) (k - 1)]);
}

// Scale degree of a key (nullopt = unmapped).
std::optional<int> keyDegree (const Scale& sc, const KeyboardMap& km, int key)
{
    if (km.size <= 0)
        return key - km.middle;
    if (key < km.first || key > km.last) return std::nullopt;
    const int d = key - km.middle;
    const int oct = (int) std::floor ((double) d / km.size);
    const int idx = d - oct * km.size;
    const int m = idx < (int) km.mapping.size() ? km.mapping[(size_t) idx] : -1;
    if (m < 0) return std::nullopt;
    const int octDeg = km.octaveDegree > 0 ? km.octaveDegree : (int) sc.cents.size();
    return oct * octDeg + m;
}
} // namespace

std::optional<Scale> parseScl (const juce::String& text, juce::String* error)
{
    const auto lines = contentLines (text);
    if (lines.size() < 2) { setError (error, "File .scl incompleto"); return std::nullopt; }
    Scale sc;
    sc.description = lines[0];
    const int n = lines[1].getIntValue();
    if (n <= 0 || n > 1024 || lines.size() < 2 + n) { setError (error, "Numero di note .scl non valido"); return std::nullopt; }
    for (int i = 0; i < n; ++i)
    {
        const auto c = parsePitch (lines[2 + i]);
        if (! c) { setError (error, "Valore non valido alla riga: " + lines[2 + i]); return std::nullopt; }
        sc.cents.push_back (*c);
    }
    if (sc.cents.back() <= 0.0) { setError (error, "Periodo della scala non valido"); return std::nullopt; }
    return sc;
}

std::optional<KeyboardMap> parseKbm (const juce::String& text, juce::String* error)
{
    juce::StringArray l;
    for (auto& s : contentLines (text)) if (s.isNotEmpty()) l.add (s);
    if (l.size() < 7) { setError (error, "File .kbm incompleto"); return std::nullopt; }
    KeyboardMap km;
    km.size = juce::jlimit (0, 1024, l[0].getIntValue());
    km.first = juce::jlimit (0, 127, l[1].getIntValue());
    km.last = juce::jlimit (0, 127, l[2].getIntValue());
    km.middle = juce::jlimit (0, 127, l[3].getIntValue());
    km.reference = juce::jlimit (0, 127, l[4].getIntValue());
    km.referenceHz = l[5].getDoubleValue();
    km.octaveDegree = juce::jmax (0, l[6].getIntValue());
    if (km.referenceHz <= 0.0) { setError (error, "Frequenza di riferimento .kbm non valida"); return std::nullopt; }
    for (int i = 0; i < km.size; ++i)
    {
        const auto s = i + 7 < l.size() ? l[i + 7].trim() : juce::String ("x");
        km.mapping.push_back (s.startsWithIgnoreCase ("x") ? -1 : s.getIntValue());
    }
    return km;
}

TuningTable fromScale (const Scale& sc, const std::optional<KeyboardMap>& kbm)
{
    const KeyboardMap km = kbm.value_or (KeyboardMap {});
    TuningTable t;
    auto refDeg = keyDegree (sc, km, km.reference);
    // Reference key unmapped: fall back to its linear degree so the table stays defined.
    const double refCents = degreeCents (sc, refDeg.value_or (km.reference - km.middle));
    const double refPitch = 69.0 + 12.0 * std::log2 (km.referenceHz / 440.0);
    for (int k = 0; k < 128; ++k)
    {
        const auto d = keyDegree (sc, km, k);
        if (! d) { t.semis[(size_t) k] = 0.0f; continue; }
        const double pitch = refPitch + (degreeCents (sc, *d) - refCents) / 100.0;
        t.semis[(size_t) k] = (float) juce::jlimit (-128.0, 128.0, pitch - k);
    }
    return t;
}

std::optional<TuningTable> parseTun (const juce::String& text, juce::String* error)
{
    std::array<std::optional<double>, 128> plain {}, exact {};
    double base = 8.1757989156;
    juce::String section;
    bool any = false;
    for (auto l : juce::StringArray::fromLines (text))
    {
        l = l.upToFirstOccurrenceOf (";", false, false).trim();
        if (l.isEmpty()) continue;
        if (l.startsWithChar ('[')) { section = l.toLowerCase(); continue; }
        const auto key = l.upToFirstOccurrenceOf ("=", false, false).trim().toLowerCase();
        const auto val = l.fromFirstOccurrenceOf ("=", false, false).trim();
        if (section == "[exact tuning]" && key == "basefreq") { base = val.getDoubleValue(); continue; }
        if (! key.startsWith ("note") || (section != "[tuning]" && section != "[exact tuning]")) continue;
        const int n = key.fromFirstOccurrenceOf ("note", false, false).trim().getIntValue();
        if (n < 0 || n > 127) continue;
        (section == "[tuning]" ? plain : exact)[(size_t) n] = val.getDoubleValue();
        any = true;
    }
    if (! any || base <= 0.0) { setError (error, "Nessuna nota trovata nel file .tun"); return std::nullopt; }
    TuningTable t;
    const double basePitch = 12.0 * std::log2 (base / 8.1757989156);
    for (int k = 0; k < 128; ++k)
    {
        const auto& c = exact[(size_t) k] ? exact[(size_t) k] : plain[(size_t) k];
        if (c) t.semis[(size_t) k] = (float) juce::jlimit (-128.0, 128.0, basePitch + *c / 100.0 - k);
    }
    return t;
}

std::optional<TuningTable> loadFile (const juce::File& f, juce::String* error)
{
    const auto text = f.loadFileAsString();
    if (f.hasFileExtension ("tun")) return parseTun (text, error);
    auto sc = parseScl (text, error);
    if (! sc) return std::nullopt;
    std::optional<KeyboardMap> km;
    const auto kf = f.withFileExtension ("kbm");
    if (kf.existsAsFile()) km = parseKbm (kf.loadFileAsString(), error);
    return fromScale (*sc, km);
}
} // namespace chroma::tuning
