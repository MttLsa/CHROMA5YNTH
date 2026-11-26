#pragma once
#include "PresetManager.h"
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

namespace chroma
{
// §6 loudness normalisation (tools/presetnorm + tests). Offline, message thread.
// Each preset is rendered on a fresh processor (non-realtime: offline oversampling), note C3 (MIDI 60),
// velocity 100, held for 4 s at 48 kHz; the trim is chosen so that the max short-term loudness is
// −14 LUFS without going above −1 dBTP: peaks stay below the output limiter threshold, so the
// limiter never changes the sound of a factory preset.
// Multi presets: host parameter preset_trim. Layer presets: layer parameter layer_trim.
struct PresetNormalizer
{
    static constexpr double targetLufs = -14.0, ceilingDbtp = -1.0, peakMargin = 0.1;
    static constexpr double sampleRate = 48000.0, seconds = 4.0;
    static constexpr int note = 60, velocity = 100, blockSize = 512;

    struct Measure { double lufs = -200.0, truePeak = -240.0, limiterDb = 0.0; };   // limiterDb: max gain reduction (≤ 0)
    struct Result
    {
        Measure before, after;
        float trimDb = 0.0f;
        int iterations = 0;
        bool peakLimited = false;   // trim capped by the true-peak ceiling
        bool silent = false;
    };

    // Renders the content (Multi state tree or LAYER_PRESET, loaded in layer 1) and measures it.
    static std::vector<std::vector<float>> render (PresetKind, const juce::ValueTree& content, float* limiterMinGain = nullptr);
    static Measure measure (PresetKind, const juce::ValueTree& content);

    // Writes the trim into a copy of the content and returns it (result in *res).
    static juce::ValueTree normalize (PresetKind, const juce::ValueTree& content, Result* res = nullptr);
    static float getTrim (PresetKind, const juce::ValueTree& content);
    static void setTrim (PresetKind, juce::ValueTree& content, float db);

    // Removes every value equal to its default (sparse files: missing values load as defaults).
    static juce::ValueTree compact (PresetKind, const juce::ValueTree& content);

    // Normalises preset files in place (keeps metadata). Returns false on read/write failure.
    static bool normalizeFile (const juce::File&, Result* res = nullptr);
};
} // namespace chroma
