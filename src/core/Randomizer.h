#pragma once
#include "Synth.h"

namespace chroma
{
// §5 Randomize (message thread). Values move towards a random target by `amount` (0 = unchanged,
// 1 = fully random) in normalised space; discrete params jump with probability `amount`.
// Never touched: master level, polyphony, Scale, MPE, Global settings, macros, automation slots,
// layer On/Engine/Volume/Mute/Solo/Transpose/ranges/channel/voices.
struct RandomizeScope
{
    enum Kind { Global, Layer, Section } kind = Global;
    int layer = 0;            // Layer / Section (layer internal sections)
    juce::String section;     // Section: exact name or prefix ("Analog" = all "Analog · …")
};

class Randomizer
{
public:
    static bool sectionMatches (const juce::String& paramSection, const juce::String& wanted);
    // Returns the number of parameters changed.
    static int apply (Synth&, juce::AudioProcessorValueTreeState&, const RandomizeScope&, float amount, juce::Random&);
    // Distinct internal layer sections ("Analog · OSC 1", …) and host sections that can be randomized.
    static juce::StringArray layerSections();
    static juce::StringArray hostSections();
};
} // namespace chroma
