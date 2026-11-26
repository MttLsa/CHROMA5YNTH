#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <optional>

namespace chroma
{
// §5 Microtuning: per-MIDI-note offset in semitones from 12-TET (A4 = 440 Hz).
// Pitch actually played = note + semis[note] (+ master tune offset, added by the synth).
struct TuningTable
{
    std::array<float, 128> semis {};
};

namespace tuning
{
// Scala .scl: returns the scale degrees in cents (degree 1..N, last = period), or nullopt.
struct Scale
{
    juce::String description;
    std::vector<double> cents;   // N entries, cents[N-1] = period
};
std::optional<Scale> parseScl (const juce::String& text, juce::String* error = nullptr);

// Scala .kbm keyboard mapping. mapping[i] = scale degree, -1 = unmapped ('x').
struct KeyboardMap
{
    int size = 0, first = 0, last = 127, middle = 60, reference = 69, octaveDegree = 0;
    double referenceHz = 440.0;
    std::vector<int> mapping;
};
std::optional<KeyboardMap> parseKbm (const juce::String& text, juce::String* error = nullptr);

// Builds the table from a scale + optional mapping. Default mapping: degree 0 on note 60,
// A4 (note 69) = 440 Hz, linear. Unmapped keys keep 12-TET.
TuningTable fromScale (const Scale&, const std::optional<KeyboardMap>& = std::nullopt);

// AnaMark .tun ([Tuning] / [Exact Tuning], "note N = cents" from the base frequency,
// default 8.1757989156 Hz = MIDI note 0).
std::optional<TuningTable> parseTun (const juce::String& text, juce::String* error = nullptr);

// Loads .scl or .tun by extension (a .kbm with the same base name next to a .scl is used).
std::optional<TuningTable> loadFile (const juce::File&, juce::String* error = nullptr);
} // namespace tuning
} // namespace chroma
