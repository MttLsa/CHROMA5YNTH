#pragma once
#include "Wavetable.h"
#include <juce_core/juce_core.h>
#include <complex>
#include <vector>

namespace chroma::wt
{
// Editable wavetable (§3D editor model, message thread): frames of frameSize samples, 1..maxFrames.
class WavetableDoc
{
public:
    enum class Interp { None, Linear, Spectral };

    WavetableDoc();   // one sine frame

    int numFrames() const noexcept { return (int) (data.size() / (size_t) frameSize); }
    float* frame (int f) noexcept { return data.data() + (size_t) f * frameSize; }
    const float* frame (int f) const noexcept { return data.data() + (size_t) f * frameSize; }
    const std::vector<float>& frames() const noexcept { return data; }
    void setFrames (std::vector<float> frames, int count);

    // WAV I/O: 2048 samples per frame, up to 256 frames. Shorter files (< 2048) are read as one cycle and
    // resampled to 2048. Multichannel files are mixed to mono. Export: 32-bit float mono WAV.
    bool importWav (const juce::File&, juce::String& error);
    bool importWav (juce::InputStream*, juce::String& error);   // takes ownership
    bool exportWav (const juce::File&) const;
    bool exportWav (juce::OutputStream*) const;                 // takes ownership

    // Frames
    void addFrame (int after);          // copy of `after` inserted after it (sine if empty)
    void removeFrame (int f);           // keeps at least one frame
    void duplicateFrame (int f);

    // Resample the table to `count` frames with the given interpolation between existing frames.
    void resample (int count, Interp);
    // Morph: rewrite the frames strictly between a and b, interpolating from frame a to frame b.
    void morph (int a, int b, Interp);
    // Circular smoothing of one frame (f >= 0) or all frames (f < 0); amount 0..1 (moving average up to 33 taps).
    void smooth (int f, float amount);
    // Remove DC and scale to peak 1: per frame or with one gain for the whole table.
    void normalize (bool perFrame);
    // Draw mode: straight segment between two points (x 0..1 phase, y -1..1).
    void drawLine (int f, float x0, float y0, float x1, float y1);

    // Harmonic editor (FFT): amplitude (peak) and phase (radians, sine phase = 0) of harmonics 1..count.
    void harmonics (int f, int count, std::vector<float>& amp, std::vector<float>& phase) const;
    void setHarmonic (int f, int k, float amp, float phase);

    TablePtr build (const std::string& name) const { return buildTable (data.data(), numFrames(), name, true); }

private:
    static void interpolate (const float* a, const float* b, float t, Interp, float* out);
    std::vector<float> data;
};
} // namespace chroma::wt
