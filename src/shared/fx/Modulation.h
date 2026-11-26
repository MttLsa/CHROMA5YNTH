#pragma once
#include "FxUtil.h"

namespace chroma::fx
{
// PHASER (§2): 2-12 first-order TPT all-pass stages swept exponentially by a sine LFO, feedback.
struct PhaserParams { float rateHz = 0.3f, depth = 0.7f, feedback = 0.5f, freqHz = 800.0f, mix = 0.5f; int stages = 4; };

class Phaser
{
public:
    static constexpr int maxStages = 12;

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        for (auto* s : { &rate, &depth, &fb, &freq, &mixS }) s->setTime (0.05f, sampleRate);
        reset();
    }
    void reset() noexcept
    {
        for (auto& c : state) c.fill (0.0f);
        last = { 0.0f, 0.0f };
        G = { 0.0f, 0.0f };
        lfo.reset();
        counter = 0;
        first = true;
    }
    void setParams (const PhaserParams& p) noexcept
    {
        rate.target = p.rateHz; depth.target = p.depth; fb.target = p.feedback;
        freq.target = std::log2 (p.freqHz); mixS.target = p.mix;
        stages = std::clamp (p.stages, 2, maxStages);
        if (first) { for (auto* s : { &rate, &depth, &fb, &freq, &mixS }) s->reset (s->target); first = false; }
    }
    void process (float* l, float* r, int n) noexcept
    {
        float* io[2] { l, r };
        for (int i = 0; i < n; ++i)
        {
            const float rt = rate.next(), dp = depth.next(), f = fb.next(), lf = freq.next(), m = mixS.next();
            lfo.advance (rt, sr);
            if (counter-- <= 0)
            {
                counter = 3;
                for (int c = 0; c < 2; ++c)
                {
                    const float oct = lf + 3.0f * dp * FxLfo::sine (lfo.phase + 0.25 * c);
                    const float hz = std::clamp (std::exp2 (oct), 20.0f, 0.45f * sr);
                    const float g = std::tan (pi * hz / sr);
                    G[(size_t) c] = g / (1.0f + g);
                }
            }
            for (int c = 0; c < 2; ++c)
            {
                const float x = io[c][i];
                float y = x + f * last[(size_t) c];
                auto& s = state[(size_t) c];
                const float g = G[(size_t) c];
                for (int k = 0; k < stages; ++k)
                {
                    const float v = (y - s[(size_t) k]) * g;
                    const float lp = v + s[(size_t) k];
                    s[(size_t) k] = flushDenormal (lp + v);
                    y = 2.0f * lp - y;
                }
                last[(size_t) c] = std::tanh (y);
                io[c][i] = x + m * (y - x);
            }
        }
    }

private:
    std::array<std::array<float, maxStages>, 2> state {};
    std::array<float, 2> last {}, G {};
    FxLfo lfo;
    Smoothed rate, depth, fb, freq, mixS;
    int stages = 4, counter = 0;
    float sr = 48000.0f;
    bool first = true;
};

// FLANGER (§2): short modulated delay with feedback, L/R LFO offset.
struct FlangerParams { float rateHz = 0.2f, depth = 0.6f, feedback = 0.5f, delayMs = 1.5f, stereo = 0.5f, mix = 0.5f; };

class Flanger
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        for (auto& d : lines) d.prepare ((int) (0.02 * sampleRate) + 8);
        for (auto* s : { &rate, &depth, &fb, &delay, &stereo, &mixS }) s->setTime (0.05f, sampleRate);
        reset();
    }
    void reset() noexcept
    {
        for (auto& d : lines) d.clear();
        wet = { 0.0f, 0.0f };
        lfo.reset();
        first = true;
    }
    void setParams (const FlangerParams& p) noexcept
    {
        rate.target = p.rateHz; depth.target = p.depth; fb.target = p.feedback;
        delay.target = p.delayMs; stereo.target = 0.5f * p.stereo; mixS.target = p.mix;
        if (first) { for (auto* s : { &rate, &depth, &fb, &delay, &stereo, &mixS }) s->reset (s->target); first = false; }
    }
    void process (float* l, float* r, int n) noexcept
    {
        float* io[2] { l, r };
        const float msToS = sr * 0.001f;
        for (int i = 0; i < n; ++i)
        {
            const float rt = rate.next(), dp = depth.next(), f = fb.next(), dl = delay.next(), st = stereo.next(), m = mixS.next();
            lfo.advance (rt, sr);
            for (int c = 0; c < 2; ++c)
            {
                const float mod = 0.5f + 0.5f * FxLfo::sine (lfo.phase + (c == 1 ? st : 0.0f));
                const float x = io[c][i];
                lines[(size_t) c].push (x + f * wet[(size_t) c]);
                const float y = lines[(size_t) c].read ((dl + dp * 7.0f * mod) * msToS);
                wet[(size_t) c] = flushDenormal (std::tanh (y));
                io[c][i] = x + m * (y - x);
            }
        }
    }

private:
    std::array<DelayLine, 2> lines;
    std::array<float, 2> wet {};
    FxLfo lfo;
    Smoothed rate, depth, fb, delay, stereo, mixS;
    float sr = 48000.0f;
    bool first = true;
};
} // namespace chroma::fx
