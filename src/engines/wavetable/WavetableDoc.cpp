#include "WavetableDoc.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>

namespace chroma::wt
{
namespace
{
constexpr float twoPiF = 6.2831853f;

void sineFrame (float* out)
{
    for (int i = 0; i < frameSize; ++i) out[i] = std::sin (twoPiF * (float) i / (float) frameSize);
}

// Unwrapped-free phase interpolation: shortest angular path.
float lerpAngle (float a, float b, float t)
{
    float d = std::remainder (b - a, twoPiF);
    return a + d * t;
}

// Full (non band-limited) synthesis of a 2048 frame from harmonics 1..1023.
void synthFull (const std::vector<std::complex<float>>& c, float* out)
{
    std::vector<float> w ((size_t) frameSize * 2 + 8);
    std::vector<float> tmp ((size_t) frameSize + 4);
    synthesize (c.data(), frameSize / 2 - 1, tmp.data() + 1, frameSize, w.data());
    std::copy (tmp.begin() + 1, tmp.begin() + 1 + frameSize, out);
}

void analyseFull (const float* frame, std::vector<std::complex<float>>& c)
{
    c.assign ((size_t) frameSize / 2, {});
    wt::analyseFull (frame, c.data());
}
} // namespace

WavetableDoc::WavetableDoc()
{
    data.assign ((size_t) frameSize, 0.0f);
    sineFrame (data.data());
}

void WavetableDoc::setFrames (std::vector<float> frames, int count)
{
    count = juce::jlimit (0, maxFrames, juce::jmin (count, (int) (frames.size() / (size_t) frameSize)));
    if (count == 0) { *this = WavetableDoc(); return; }
    frames.resize ((size_t) count * frameSize);
    data = std::move (frames);
}

// ------------------------------------------------------------------ WAV
bool WavetableDoc::importWav (const juce::File& f, juce::String& error)
{
    return importWav (f.createInputStream().release(), error);
}

bool WavetableDoc::importWav (juce::InputStream* in, juce::String& error)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> r (in != nullptr ? wav.createReaderFor (in, true) : nullptr);
    if (r == nullptr) { error = "File WAV non leggibile."; return false; }
    const auto len = (int) juce::jmin<juce::int64> (r->lengthInSamples, (juce::int64) frameSize * maxFrames);
    if (len <= 1) { error = "File WAV vuoto."; return false; }
    juce::AudioBuffer<float> buf ((int) r->numChannels, len);
    r->read (&buf, 0, len, 0, true, true);
    std::vector<float> mono ((size_t) len, 0.0f);
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        for (int i = 0; i < len; ++i) mono[(size_t) i] += buf.getSample (ch, i) / (float) buf.getNumChannels();

    if (len < frameSize)   // single cycle: resample (linear, circular) to frameSize
    {
        std::vector<float> fr ((size_t) frameSize);
        for (int i = 0; i < frameSize; ++i)
        {
            const float x = (float) i * (float) len / (float) frameSize;
            const int i0 = (int) x;
            const float t = x - (float) i0;
            fr[(size_t) i] = mono[(size_t) i0] + t * (mono[(size_t) ((i0 + 1) % len)] - mono[(size_t) i0]);
        }
        data = std::move (fr);
    }
    else
    {
        const int nf = len / frameSize;
        mono.resize ((size_t) nf * frameSize);
        data = std::move (mono);
        if (len % frameSize != 0) error = "Coda del file ignorata (non multipla di 2048 campioni).";
    }
    for (auto& x : data) if (! std::isfinite (x)) x = 0.0f;
    return true;
}

bool WavetableDoc::exportWav (const juce::File& f) const
{
    f.deleteFile();
    return exportWav (f.createOutputStream().release());
}

bool WavetableDoc::exportWav (juce::OutputStream* out) const
{
    if (out == nullptr) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (out, 48000.0, 1, 32, {}, 0));
    if (w == nullptr) { delete out; return false; }
    const float* ch[1] = { data.data() };
    return w->writeFromFloatArrays (ch, 1, (int) data.size());
}

// ------------------------------------------------------------------ frames
void WavetableDoc::addFrame (int after)
{
    if (numFrames() >= maxFrames) return;
    after = juce::jlimit (0, numFrames() - 1, after);
    std::vector<float> fr (frame (after), frame (after) + frameSize);
    data.insert (data.begin() + (std::ptrdiff_t) ((size_t) (after + 1) * frameSize), fr.begin(), fr.end());
}

void WavetableDoc::duplicateFrame (int f) { addFrame (f); }

void WavetableDoc::removeFrame (int f)
{
    if (numFrames() <= 1 || f < 0 || f >= numFrames()) return;
    const auto b = data.begin() + (std::ptrdiff_t) ((size_t) f * frameSize);
    data.erase (b, b + frameSize);
}

void WavetableDoc::interpolate (const float* a, const float* b, float t, Interp mode, float* out)
{
    if (mode == Interp::None)
    {
        std::copy (t < 0.5f ? a : b, (t < 0.5f ? a : b) + frameSize, out);
        return;
    }
    if (mode == Interp::Linear)
    {
        for (int i = 0; i < frameSize; ++i) out[i] = a[i] + t * (b[i] - a[i]);
        return;
    }
    // Spectral: magnitudes and phases interpolated separately (no cancellation between frames).
    std::vector<std::complex<float>> ca, cb;
    analyseFull (a, ca);
    analyseFull (b, cb);
    std::vector<std::complex<float>> c (ca.size());
    for (size_t k = 1; k < c.size(); ++k)
    {
        const float m = std::abs (ca[k]) + t * (std::abs (cb[k]) - std::abs (ca[k]));
        const float pa = std::abs (ca[k]) > 1.0e-9f ? std::arg (ca[k]) : std::arg (cb[k]);
        const float pb = std::abs (cb[k]) > 1.0e-9f ? std::arg (cb[k]) : pa;
        c[k] = std::polar (m, lerpAngle (pa, pb, t));
    }
    synthFull (c, out);
    const float dc = ca[0].real() + t * (cb[0].real() - ca[0].real());
    for (int i = 0; i < frameSize; ++i) out[i] += dc;
}

void WavetableDoc::resample (int count, Interp mode)
{
    count = juce::jlimit (1, maxFrames, count);
    const int n = numFrames();
    std::vector<float> out ((size_t) count * frameSize);
    for (int f = 0; f < count; ++f)
    {
        const float x = count > 1 ? (float) f * (float) (n - 1) / (float) (count - 1) : 0.0f;
        const int i0 = juce::jmin ((int) x, n - 1);
        const int i1 = juce::jmin (i0 + 1, n - 1);
        interpolate (frame (i0), frame (i1), x - (float) i0, mode, out.data() + (size_t) f * frameSize);
    }
    data = std::move (out);
}

void WavetableDoc::morph (int a, int b, Interp mode)
{
    const int n = numFrames();
    a = juce::jlimit (0, n - 1, a);
    b = juce::jlimit (0, n - 1, b);
    if (std::abs (b - a) < 2) return;
    std::vector<float> fa (frame (a), frame (a) + frameSize), fb (frame (b), frame (b) + frameSize);
    const int step = b > a ? 1 : -1;
    for (int f = a + step; f != b; f += step)
        interpolate (fa.data(), fb.data(), (float) std::abs (f - a) / (float) std::abs (b - a), mode, frame (f));
}

void WavetableDoc::smooth (int f, float amount)
{
    const int half = juce::roundToInt (juce::jlimit (0.0f, 1.0f, amount) * 16.0f);
    if (half <= 0) return;
    std::vector<float> tmp ((size_t) frameSize);
    const auto one = [&] (float* x)
    {
        double acc = 0.0;
        for (int j = -half; j <= half; ++j) acc += x[(j + frameSize) % frameSize];
        const float inv = 1.0f / (float) (2 * half + 1);
        for (int i = 0; i < frameSize; ++i)
        {
            tmp[(size_t) i] = (float) acc * inv;
            acc += x[(i + half + 1) % frameSize] - x[(i - half + frameSize) % frameSize];
        }
        std::copy (tmp.begin(), tmp.end(), x);
    };
    if (f >= 0 && f < numFrames()) one (frame (f));
    else for (int i = 0; i < numFrames(); ++i) one (frame (i));
}

void WavetableDoc::normalize (bool perFrame)
{
    float global = 0.0f;
    for (int f = 0; f < numFrames(); ++f)
    {
        float* x = frame (f);
        double dc = 0.0;
        for (int i = 0; i < frameSize; ++i) dc += x[i];
        const float d = (float) (dc / frameSize);
        float pk = 0.0f;
        for (int i = 0; i < frameSize; ++i) { x[i] -= d; pk = std::max (pk, std::fabs (x[i])); }
        if (perFrame && pk > 1.0e-9f)
            for (int i = 0; i < frameSize; ++i) x[i] /= pk;
        global = std::max (global, pk);
    }
    if (! perFrame && global > 1.0e-9f)
        for (auto& v : data) v /= global;
}

void WavetableDoc::drawLine (int f, float x0, float y0, float x1, float y1)
{
    if (f < 0 || f >= numFrames()) return;
    if (x1 < x0) { std::swap (x0, x1); std::swap (y0, y1); }
    float* x = frame (f);
    const int i0 = juce::jlimit (0, frameSize - 1, (int) std::floor (x0 * frameSize));
    const int i1 = juce::jlimit (0, frameSize - 1, (int) std::floor (x1 * frameSize));
    for (int i = i0; i <= i1; ++i)
    {
        const float t = i1 > i0 ? (float) (i - i0) / (float) (i1 - i0) : 0.0f;
        x[i] = juce::jlimit (-1.0f, 1.0f, y0 + t * (y1 - y0));
    }
}

// ------------------------------------------------------------------ harmonics
// Convention: x(t) = sum a_k sin(2 pi k t + phi_k)  ->  c_k = a_k e^{i(phi_k - pi/2)}.
void WavetableDoc::harmonics (int f, int count, std::vector<float>& amp, std::vector<float>& phase) const
{
    std::vector<std::complex<float>> c;
    analyseFull (frame (juce::jlimit (0, numFrames() - 1, f)), c);
    count = juce::jlimit (1, frameSize / 2 - 1, count);
    amp.assign ((size_t) count, 0.0f);
    phase.assign ((size_t) count, 0.0f);
    for (int k = 1; k <= count; ++k)
    {
        amp[(size_t) (k - 1)] = std::abs (c[(size_t) k]);
        phase[(size_t) (k - 1)] = std::remainder (std::arg (c[(size_t) k]) + twoPiF * 0.25f, twoPiF);
    }
}

void WavetableDoc::setHarmonic (int f, int k, float amp, float phase)
{
    if (f < 0 || f >= numFrames() || k < 1 || k >= frameSize / 2) return;
    std::vector<std::complex<float>> c;
    analyseFull (frame (f), c);
    c[(size_t) k] = std::polar (std::max (0.0f, amp), phase - twoPiF * 0.25f);
    const float dc = c[0].real();
    synthFull (c, frame (f));
    for (int i = 0; i < frameSize; ++i) frame (f)[i] += dc;
}
} // namespace chroma::wt
