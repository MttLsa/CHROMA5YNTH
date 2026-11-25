#pragma once
#include "FxUtil.h"
#include "shared/Oversampler.h"
#include <array>

namespace chroma::fx
{
// Shared waveshaper for Layer Distortion (§2) and Master Drive (§4).
// Technical references: OpAmp = RAT-style op-amp gain + diode clip, Pedal = DS-1-style hard clip.
// Non-linear types run at 2x (halfband up/down), Bitcrush at the base rate.
enum class DistType { Soft, Hard, Tube, Fuzz, OpAmp, Pedal, Bitcrush, Overdrive, Tape };

// Order of `dist_types` (Layer) and `master_drive_types` (Master).
inline DistType layerDistType (int i) noexcept { return (DistType) std::clamp (i, 0, 6); }
inline DistType masterDriveType (int i) noexcept
{
    static constexpr DistType t[] { DistType::Tube, DistType::Overdrive, DistType::OpAmp, DistType::Pedal, DistType::Fuzz, DistType::Tape };
    return t[std::clamp (i, 0, 5)];
}

struct DistortionParams { DistType type = DistType::Soft; float drive = 0.3f, tone = 0.5f, mix = 1.0f, outputDb = 0.0f; };

// 2x halfband interpolator (same Kaiser design as the oscillator decimator).
class Upsampler2x
{
public:
    static constexpr int histLen = 48;   // (95 + 1) / 2 input samples cover the halfband taps
    void reset() noexcept { hist.fill (0.0f); pos = 0; }
    void process (float x, float& even, float& odd) noexcept
    {
        const auto& hb = Halfband::longStage();
        const int L = hb.length;
        pos = (pos == 0 ? histLen : pos) - 1;
        hist[(size_t) pos] = hist[(size_t) (pos + histLen)] = x;   // mirrored: contiguous newest-first window
        const float* h = hist.data() + pos;
        const float* t = hb.taps.data();
        float e = 0.0f, o = 0.0f;
        for (int k = 0; k < L; k += 2) e += t[k] * h[k >> 1];
        for (int k = 1; k < L; k += 2) o += t[k] * h[k >> 1];
        even = 2.0f * e;
        odd = 2.0f * o;
    }
private:
    std::array<float, 2 * histLen> hist {};
    int pos = 0;
};

class Distortion
{
public:
    static constexpr int maxBlock = 64;

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        for (auto& c : ch) { c.os.assign (2 * maxBlock, 0.0f); c.work.assign ((size_t) Decimator::workSize (2 * maxBlock), 0.0f); }
        mix.setTime (0.01f, sampleRate);
        out.setTime (0.01f, sampleRate);
        gain.setTime (0.01f, sampleRate);
        for (auto& c : ch) c.dry.prepare (maxBlock + 128);
        latency = measureLatency();
        reset();
    }

    int getOversamplingLatency() const noexcept { return latency; }

    void reset() noexcept
    {
        for (auto& c : ch)
        {
            c.up.reset(); c.down.reset(); c.pre.reset(); c.preHp.reset(); c.toneLp.reset(); c.shelf.reset(); c.dc.reset();
            c.hyst = 0.0f; c.held = 0.0f; c.holdCount = 0.0f;
            c.dry.clear();
        }
        first = true;
    }

    void setParams (const DistortionParams& p) noexcept
    {
        params = p;
        const float g = dbToGain (p.drive * 40.0f);
        mix.target = p.mix;
        out.target = dbToGain (p.outputDb);
        gain.target = g;
        if (first) { mix.reset (p.mix); out.reset (out.target); gain.reset (g); first = false; }

        // Pre-filters (run at 2x): op-amp gain-bandwidth roll-off, pedal bass tightening.
        const float osr = 2.0f * sr;
        for (auto& c : ch)
        {
            c.pre.set (std::clamp (1.0e6f / std::max (g, 1.0f), 2000.0f, 0.45f * osr), osr);
            c.preHp.set (p.type == DistType::Pedal ? 120.0f : p.type == DistType::OpAmp ? 60.0f : 10.0f, osr);
            c.toneLp.set (1000.0f * std::pow (20.0f, std::min (p.tone * 2.0f, 1.0f)), sr);
            c.shelf.design (Biquad::Kind::HighShelf, 2500.0f, 0.7f, std::max (0.0f, p.tone - 0.5f) * 18.0f, sr);
            c.dc.set (8.0f, sr);
        }
        // Loudness compensation: a -6 dBFS peak keeps roughly the same level.
        norm = 0.5f / std::max (1.0e-3f, std::fabs (shape (0.5f * g, ch[0], true)));
        if (p.type == DistType::Bitcrush) norm = 1.0f;
    }

    void process (float* l, float* r, int n) noexcept
    {
        for (int start = 0; start < n; start += maxBlock)
        {
            const int len = std::min (maxBlock, n - start);
            run (l + start, r + start, len);
        }
    }

private:
    struct Channel
    {
        Upsampler2x up;
        Decimator down;
        OnePole pre, preHp, toneLp, dc;
        Biquad shelf;
        float hyst = 0.0f, held = 0.0f, holdCount = 0.0f;
        std::vector<float> os, work;
        DelayLine dry;   // dry path aligned with the 2x up/down latency
    };

    // Integer latency of the 2x up -> down chain (impulse peak), used to align the dry signal.
    static int measureLatency()
    {
        Upsampler2x up;
        Decimator down;
        std::vector<float> os (256), y (128), work ((size_t) Decimator::workSize (256));
        for (int i = 0; i < 128; ++i) up.process (i == 0 ? 1.0f : 0.0f, os[(size_t) (2 * i)], os[(size_t) (2 * i + 1)]);
        down.process (os.data(), y.data(), 128, 2, work.data());
        return (int) (std::max_element (y.begin(), y.end(), [] (float a, float b) { return std::fabs (a) < std::fabs (b); }) - y.begin());
    }

    float shape (float x, Channel& c, bool probe = false) const noexcept
    {
        switch (params.type)
        {
            case DistType::Soft: return std::tanh (x);
            case DistType::Hard: return std::clamp (x, -1.0f, 1.0f);
            case DistType::Tube: return std::tanh (x + 0.35f) - 0.33638f;   // tanh(0.35): asymmetric, even harmonics
            case DistType::Fuzz:
            {
                const float y = x > 0.0f ? 1.0f - std::exp (-2.0f * x) : -0.8f * (1.0f - std::exp (1.6f * x));
                return std::tanh (2.0f * y);
            }
            case DistType::OpAmp: return x / std::pow (1.0f + std::pow (std::fabs (x), 2.5f), 0.4f);
            case DistType::Pedal: { const float a = std::fabs (x); return x / std::pow (1.0f + a * a * a * a * a * a, 1.0f / 6.0f); }
            case DistType::Overdrive:
            {
                const float y = std::clamp (x * 0.75f, -1.0f, 1.0f);
                return 1.5f * (y - y * y * y / 3.0f);
            }
            case DistType::Tape:
            {
                const float y = std::tanh (x - 0.15f * c.hyst);
                if (! probe) c.hyst = y;
                return 0.9f * y + 0.1f * x / (1.0f + std::fabs (x));
            }
            case DistType::Bitcrush: default: return x;
        }
    }

    void crush (float* x, int n, Channel& c, float drive) noexcept
    {
        const float bits = 16.0f - drive * 14.0f;
        const float q = std::pow (2.0f, bits - 1.0f);
        const float hold = 1.0f + drive * drive * 31.0f;
        for (int i = 0; i < n; ++i)
        {
            if ((c.holdCount -= 1.0f) <= 0.0f) { c.holdCount += hold; c.held = std::round (x[i] * q) / q; }
            x[i] = c.held;
        }
    }

    void run (float* l, float* r, int n) noexcept
    {
        std::array<float, maxBlock> dryL, dryR;
        const int d = params.type == DistType::Bitcrush ? 0 : latency;
        for (int i = 0; i < n; ++i)
        {
            ch[0].dry.push (l[i]); ch[1].dry.push (r[i]);
            dryL[(size_t) i] = ch[0].dry.tap (d);
            dryR[(size_t) i] = ch[1].dry.tap (d);
        }
        const float g0 = gain.value;
        float* io[2] { l, r };

        for (int c = 0; c < 2; ++c)
        {
            auto& C = ch[(size_t) c];
            float* x = io[c];
            if (params.type == DistType::Bitcrush)
                crush (x, n, C, params.drive);
            else
            {
                Smoothed gs = gain;
                gs.value = g0;
                for (int i = 0; i < n; ++i)
                {
                    const float gi = gs.next();
                    float e, o;
                    C.up.process (x[i], e, o);
                    float s[2] { e, o };
                    for (float& v : s)
                    {
                        v = C.preHp.hp (v) * gi;
                        if (params.type == DistType::OpAmp) v = C.pre.lp (v);
                        v = shape (v, C) * norm;
                    }
                    C.os[(size_t) (2 * i)] = s[0];
                    C.os[(size_t) (2 * i + 1)] = s[1];
                }
                C.down.process (C.os.data(), x, n, 2, C.work.data());
                if (c == 1) gain = gs;
            }
            for (int i = 0; i < n; ++i)
                x[i] = C.dc.hp (C.shelf.process (C.toneLp.lp (x[i])));
        }

        for (int i = 0; i < n; ++i)
        {
            const float m = mix.next(), o = out.next();
            l[i] = o * (dryL[(size_t) i] + m * (l[i] - dryL[(size_t) i]));
            r[i] = o * (dryR[(size_t) i] + m * (r[i] - dryR[(size_t) i]));
        }
    }

    DistortionParams params;
    std::array<Channel, 2> ch;
    Smoothed mix, out, gain;
    float sr = 48000.0f, norm = 1.0f;
    int latency = 0;
    bool first = true;
};
} // namespace chroma::fx
