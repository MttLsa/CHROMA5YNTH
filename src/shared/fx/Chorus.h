#pragma once
#include "FxUtil.h"

namespace chroma::fx
{
// Shared chorus for Layer Chorus (§2, Custom only) and Master Chorus (§4).
// Chorus I/II: two BBD-style lines with inverted triangle LFOs, dark and slightly saturated.
// Dimension 1-4: mono-summed input, two shallow lines in antiphase cross-mixed so the pitch
// vibrato cancels in the sum (spatial widening without wobble).
enum class ChorusMode { ChorusI, ChorusII, Dim1, Dim2, Dim3, Dim4, Custom };

struct ChorusParams
{
    ChorusMode mode = ChorusMode::Custom;
    float rateHz = 0.6f, depth = 0.5f, delayMs = 8.0f, feedback = 0.0f, width = 1.0f, mix = 0.5f;
};

class Chorus
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        for (auto& d : lines) d.prepare ((int) (0.06 * sampleRate) + 8);
        for (auto* s : { &rate, &depthMs, &centreMs, &fb, &mixS, &phaseOff })
            s->setTime (0.05f, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        for (auto& d : lines) d.clear();
        for (auto& f : tone) f.reset();
        lfo.reset();
        wet = { 0.0f, 0.0f };
        first = true;
    }

    void setParams (const ChorusParams& p) noexcept
    {
        mode = p.mode;
        float r = p.rateHz, dep = p.depth * 5.0f, centre = p.delayMs, off = 0.5f * p.width, f = p.feedback;
        switch (p.mode)
        {
            case ChorusMode::ChorusI:  r = 0.513f; dep = 1.6f; centre = 3.5f; off = 0.5f; f = 0.0f; break;
            case ChorusMode::ChorusII: r = 0.863f; dep = 1.9f; centre = 3.5f; off = 0.5f; f = 0.0f; break;
            case ChorusMode::Dim1: r = 0.25f; dep = 0.25f; centre = 6.0f; off = 0.5f; f = 0.0f; break;
            case ChorusMode::Dim2: r = 0.25f; dep = 0.45f; centre = 6.0f; off = 0.5f; f = 0.0f; break;
            case ChorusMode::Dim3: r = 0.25f; dep = 0.7f;  centre = 6.0f; off = 0.5f; f = 0.0f; break;
            case ChorusMode::Dim4: r = 0.5f;  dep = 0.9f;  centre = 6.0f; off = 0.5f; f = 0.0f; break;
            case ChorusMode::Custom: break;
        }
        if (p.mode == ChorusMode::Custom) centre = std::max (centre, dep + 0.5f);
        rate.target = r; depthMs.target = dep; centreMs.target = centre;
        fb.target = f; mixS.target = p.mix; phaseOff.target = off;
        const bool bbd = p.mode != ChorusMode::Custom;
        for (auto& t : tone) t.set (bbd ? 9000.0f : 0.45f * sr, sr);
        if (first) { for (auto* s : { &rate, &depthMs, &centreMs, &fb, &mixS, &phaseOff }) s->reset (s->target); first = false; }
    }

    void process (float* l, float* r, int n) noexcept
    {
        const bool dim = mode >= ChorusMode::Dim1 && mode <= ChorusMode::Dim4;
        const bool bbd = mode != ChorusMode::Custom;
        const float msToS = sr * 0.001f;
        for (int i = 0; i < n; ++i)
        {
            const float rt = rate.next(), dp = depthMs.next(), ce = centreMs.next(), f = fb.next(), m = mixS.next(), po = phaseOff.next();
            lfo.advance (rt, sr);
            const float ma = bbd ? FxLfo::tri (lfo.phase) : FxLfo::sine (lfo.phase);
            const float mb = bbd ? FxLfo::tri (lfo.phase + po) : FxLfo::sine (lfo.phase + po);

            const float inL = dim ? 0.5f * (l[i] + r[i]) : l[i];
            const float inR = dim ? inL : r[i];
            lines[0].push (inL + f * wet[0]);
            lines[1].push (inR + f * wet[1]);
            float a = lines[0].read ((ce + dp * ma) * msToS);
            float b = lines[1].read ((ce + dp * mb) * msToS);
            if (bbd)
            {
                a = std::tanh (tone[1].lp (tone[0].lp (a)) * 1.2f) / 1.2f;
                b = std::tanh (tone[3].lp (tone[2].lp (b)) * 1.2f) / 1.2f;
            }
            wet = { flushDenormal (a), flushDenormal (b) };
            float wl = a, wr = b;
            if (dim) { wl = a - 0.5f * b; wr = b - 0.5f * a; }
            l[i] += m * (wl - l[i]);
            r[i] += m * (wr - r[i]);
        }
    }

private:
    std::array<DelayLine, 2> lines;
    std::array<OnePole, 4> tone;
    FxLfo lfo;
    Smoothed rate, depthMs, centreMs, fb, mixS, phaseOff;
    std::array<float, 2> wet {};
    ChorusMode mode = ChorusMode::Custom;
    float sr = 48000.0f;
    bool first = true;
};
} // namespace chroma::fx
