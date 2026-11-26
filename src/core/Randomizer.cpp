#include "Randomizer.h"
#include "GuiLayout.h"
#include "shared/ParamRange.h"

namespace chroma
{
namespace
{
juce::String u8 (const char* t) { return juce::String::fromUTF8 (t); }

// Sections left alone by Global/Layer randomize (playability), still reachable as an explicit Section.
bool protectedLayerSection (const juce::String& s)
{
    return s == u8 ("Layer \xc2\xb7 Voice") || s == u8 ("Layer \xc2\xb7 Performance");
}

bool randomizableHost (const juce::RangedAudioParameter& p, const juce::String& section)
{
    if (section.startsWith ("Master FX")) return true;
    for (const auto* id : ParamIDs::layerPan) if (p.getParameterID() == id) return true;
    return false;
}

juce::String hostSection (const juce::String& id)
{
    for (const auto& e : gui::params) if (id == e.id) return u8 (e.section);
    return {};
}

float towards (float v, bool discrete, int steps, float amount, juce::Random& r)
{
    if (discrete)
    {
        if (r.nextFloat() >= amount || steps < 1) return v;
        return (float) r.nextInt (steps + 1) / (float) steps;
    }
    return juce::jlimit (0.0f, 1.0f, v + amount * (r.nextFloat() - v));
}
} // namespace

bool Randomizer::sectionMatches (const juce::String& s, const juce::String& wanted)
{
    return s == wanted || s.startsWith (wanted + " ");
}

juce::StringArray Randomizer::layerSections()
{
    juce::StringArray out;
    for (const auto& d : lp::descs) out.addIfNotAlreadyThere (u8 (d.section));
    return out;
}

juce::StringArray Randomizer::hostSections()
{
    juce::StringArray out;
    for (const auto& e : gui::params)
        if (u8 (e.section).startsWith ("Master FX")) out.addIfNotAlreadyThere (u8 (e.section));
    return out;
}

int Randomizer::apply (Synth& synth, juce::AudioProcessorValueTreeState& apvts, const RandomizeScope& sc, float amount, juce::Random& rnd)
{
    amount = juce::jlimit (0.0f, 1.0f, amount);
    if (amount <= 0.0f) return 0;
    int changed = 0;

    auto doLayer = [&] (int l)
    {
        auto& store = synth.layerStore (l);
        for (int i = 0; i < lp::count; ++i)
        {
            const auto& d = lp::descs[(size_t) i];
            const auto sec = u8 (d.section);
            if (sc.kind == RandomizeScope::Section ? ! sectionMatches (sec, sc.section) : protectedLayerSection (sec))
                continue;
            const auto& r = paramRange (i);
            const int steps = (int) std::lround (d.max - d.min);
            const bool discrete = d.type != lp::Type::Float;
            const float n0 = r.toNorm (store.get (i));
            const float n1 = towards (n0, discrete, steps, amount, rnd);
            if (std::abs (n1 - n0) > 0.0f) { store.set (i, r.fromNorm (n1)); ++changed; }
        }
    };

    auto onLayer = [&] (int l) { return apvts.getParameter (ParamIDs::layerOn[(size_t) l])->getValue() > 0.5f; };

    if (sc.kind == RandomizeScope::Global)
    {
        for (int l = 0; l < numLayers; ++l) if (onLayer (l)) doLayer (l);
    }
    else if (sc.kind == RandomizeScope::Layer || (sc.kind == RandomizeScope::Section && ! sc.section.startsWith ("Master FX")))
        doLayer (juce::jlimit (0, numLayers - 1, sc.layer));

    // Host parameters: Master FX (+ layer pan for Global/Layer scope).
    for (auto* prm : apvts.processor.getParameters())
    {
        auto* p = dynamic_cast<juce::RangedAudioParameter*> (prm);
        if (p == nullptr) continue;
        const auto sec = hostSection (p->getParameterID());
        if (! randomizableHost (*p, sec)) continue;
        const bool isPan = ! sec.startsWith ("Master FX");
        if (sc.kind == RandomizeScope::Section && (isPan || ! sectionMatches (sec, sc.section))) continue;
        if (sc.kind == RandomizeScope::Layer && (! isPan || p->getParameterID() != ParamIDs::layerPan[(size_t) sc.layer])) continue;
        if (sc.kind == RandomizeScope::Global && isPan)
        {
            int l = 0;
            while (l < numLayers && p->getParameterID() != ParamIDs::layerPan[(size_t) l]) ++l;
            if (l >= numLayers || ! onLayer (l)) continue;
        }
        const int steps = p->getNumSteps() > 1 && p->getNumSteps() < 1000 ? p->getNumSteps() - 1 : 0;
        const float n0 = p->getValue();
        const float n1 = towards (n0, steps > 0, steps, amount, rnd);
        if (std::abs (n1 - n0) > 0.0f) { p->setValueNotifyingHost (n1); ++changed; }
    }
    return changed;
}
} // namespace chroma
