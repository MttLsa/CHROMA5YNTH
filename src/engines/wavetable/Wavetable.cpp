#include "Wavetable.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace chroma::wt
{
namespace
{
// Preplanned FFTs, orders 6 (64) .. 14 (16384). Created on first use (message thread: buildTable / factory()).
juce::dsp::FFT& fft (int order)
{
    static std::array<std::unique_ptr<juce::dsp::FFT>, 15> ffts = []
    {
        std::array<std::unique_ptr<juce::dsp::FFT>, 15> a;
        for (int o = 6; o < 15; ++o) a[(size_t) o] = std::make_unique<juce::dsp::FFT> (o);
        return a;
    }();
    return *ffts[(size_t) order];
}

int orderOf (int len) noexcept
{
    int o = 0;
    while ((1 << o) < len) ++o;
    return o;
}
} // namespace

void synthesize (const std::complex<float>* c, int harmonics, float* out, int len, float* work) noexcept
{
    const int H = std::min (harmonics, len / 2 - 1);
    std::fill (work, work + 2 * len, 0.0f);
    const float s = 0.5f * (float) len;
    for (int k = 1; k <= H; ++k)
    {
        work[2 * k] = c[k].real() * s;
        work[2 * k + 1] = c[k].imag() * s;
    }
    fft (orderOf (len)).performRealOnlyInverseTransform (work);
    std::copy (work, work + len, out);
    out[-1] = out[len - 1];
    out[len] = out[0];
    out[len + 1] = out[1];
    out[len + 2] = out[2];
}

void analyse (const float* frame, std::complex<float>* c)
{
    std::vector<float> buf ((size_t) frameSize * 2, 0.0f);
    std::copy (frame, frame + frameSize, buf.begin());
    fft (orderOf (frameSize)).performRealOnlyForwardTransform (buf.data());
    const float s = 2.0f / (float) frameSize;
    c[0] = {};
    for (int k = 1; k < specSize; ++k)
        c[k] = { buf[(size_t) (2 * k)] * s, buf[(size_t) (2 * k + 1)] * s };
}

void analyseFull (const float* frame, std::complex<float>* c)
{
    std::vector<float> buf ((size_t) frameSize * 2, 0.0f);
    std::copy (frame, frame + frameSize, buf.begin());
    fft (orderOf (frameSize)).performRealOnlyForwardTransform (buf.data());
    const float s = 2.0f / (float) frameSize;
    c[0] = { buf[0] / (float) frameSize, 0.0f };
    for (int k = 1; k < frameSize / 2; ++k)
        c[k] = { buf[(size_t) (2 * k)] * s, buf[(size_t) (2 * k + 1)] * s };
}

TablePtr buildTable (const float* frames, int numFrames, const std::string& name, bool normalize)
{
    auto t = std::make_shared<Table>();
    t->name = name;
    t->numFrames = std::clamp (numFrames, 1, maxFrames);
    t->mip.assign ((size_t) t->numFrames * frameStride, 0.0f);
    t->spec.assign ((size_t) t->numFrames * specSize, {});
    std::vector<float> work ((size_t) frameSize * 2 + 8);
    for (int f = 0; f < t->numFrames; ++f)
    {
        auto* c = t->spec.data() + (size_t) f * specSize;
        if (f < numFrames && frames != nullptr)
            analyse (frames + (size_t) f * frameSize, c);
        for (int l = 0; l < numLevels; ++l)
            synthesize (c, levelHarmonics (l), t->mip.data() + (size_t) f * frameStride + (size_t) levelOffset (l) + 1,
                        levelLength (l), work.data());
    }
    if (normalize)
    {
        float peak = 0.0f;
        for (int f = 0; f < t->numFrames; ++f)
        {
            const float* p = t->level (f, 0);
            for (int i = 0; i < levelLength (0); ++i) peak = std::max (peak, std::fabs (p[i]));
        }
        if (peak > 1.0e-6f)
        {
            const float g = 0.99f / peak;
            for (auto& v : t->mip) v *= g;
            for (auto& v : t->spec) v *= g;
        }
    }
    return t;
}

void TableSlot::set (TablePtr t)
{
    const uint64_t now = epoch().load (std::memory_order_acquire);
    retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (const auto& r) { return now >= r.first + 2; }),
                   retired.end());
    if (own != nullptr)
        retired.emplace_back (now, own);
    own = std::move (t);
    current.store (own.get(), std::memory_order_release);
}
} // namespace chroma::wt
