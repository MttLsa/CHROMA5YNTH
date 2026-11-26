#pragma once
#include "FxUtil.h"

namespace chroma::fx
{
// EQ (§2): Low shelf, Peak, High shelf (RBJ biquads), coefficients refreshed only on change.
struct EqParams { float lowFreq = 120.0f, lowGain = 0.0f, lowQ = 0.707f, midFreq = 1000.0f, midGain = 0.0f, midQ = 0.707f,
                  highFreq = 6000.0f, highGain = 0.0f, highQ = 0.707f; };

class Eq3
{
public:
    void prepare (double sampleRate) { sr = (float) sampleRate; reset(); }
    void reset() noexcept { for (auto& c : bands) for (auto& b : c) b.reset(); have = false; }
    void setParams (const EqParams& p) noexcept
    {
        if (have && std::memcmp (&p, &last, sizeof (EqParams)) == 0) return;
        last = p; have = true;
        for (auto& c : bands)
        {
            c[0].design (Biquad::Kind::LowShelf, p.lowFreq, p.lowQ, p.lowGain, sr);
            c[1].design (Biquad::Kind::Peak, p.midFreq, p.midQ, p.midGain, sr);
            c[2].design (Biquad::Kind::HighShelf, p.highFreq, p.highQ, p.highGain, sr);
        }
    }
    void process (float* l, float* r, int n) noexcept
    {
        float* io[2] { l, r };
        for (size_t c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                io[c][i] = bands[c][2].process (bands[c][1].process (bands[c][0].process (io[c][i])));
    }

private:
    std::array<std::array<Biquad, 3>, 2> bands;
    EqParams last;
    float sr = 48000.0f;
    bool have = false;
};

// COMPRESSOR (§2): feed-forward, stereo-linked peak detector, soft knee, attack/release in dB domain.
struct CompressorParams { float thresholdDb = -18.0f, ratio = 4.0f, attackMs = 10.0f, releaseMs = 150.0f, kneeDb = 6.0f,
                          makeupDb = 0.0f, mix = 1.0f; };

class Compressor
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        for (auto* s : { &makeup, &mixS }) s->setTime (0.02f, sampleRate);
        reset();
    }
    void reset() noexcept { gr = 0.0f; first = true; }
    void setParams (const CompressorParams& p) noexcept
    {
        params = p;
        att = std::exp (-1.0f / (std::max (p.attackMs, 0.01f) * 0.001f * sr));
        rel = std::exp (-1.0f / (std::max (p.releaseMs, 1.0f) * 0.001f * sr));
        makeup.target = dbToGain (p.makeupDb);
        mixS.target = p.mix;
        if (first) { makeup.reset (makeup.target); mixS.reset (p.mix); first = false; }
    }
    // Static curve: gain reduction (dB, >= 0) for an input level in dB.
    static float gainReduction (float xDb, float thr, float ratio, float knee) noexcept
    {
        const float over = xDb - thr;
        const float slope = 1.0f - 1.0f / std::max (ratio, 1.0f);
        if (knee > 0.0f && std::fabs (over) <= 0.5f * knee)
        {
            const float t = over + 0.5f * knee;
            return slope * t * t / (2.0f * knee);
        }
        return over > 0.0f ? slope * over : 0.0f;
    }
    float getGainReductionDb() const noexcept { return gr; }

    void process (float* l, float* r, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float lvl = std::max (std::fabs (l[i]), std::fabs (r[i]));
            const float target = gainReduction (gainToDb (lvl), params.thresholdDb, params.ratio, params.kneeDb);
            gr = target > gr ? target + (gr - target) * att : target + (gr - target) * rel;
            const float g = dbToGain (-gr) * makeup.next();
            const float m = mixS.next();
            l[i] *= 1.0f + m * (g - 1.0f);
            r[i] *= 1.0f + m * (g - 1.0f);
        }
    }

private:
    CompressorParams params;
    Smoothed makeup, mixS;
    float sr = 48000.0f, att = 0.0f, rel = 0.0f, gr = 0.0f;
    bool first = true;
};

// MASTER FILTER (§4): HP -> BP -> LP, 12 dB/oct TPT SVF with resonance, each section On/Off.
// HP rises from 20 Hz, LP falls from 20 kHz, BP = HP+LP pair around the centre with a width
// from +-5 octaves (open) to +-1/6 octave.
struct MasterFilterParams
{
    bool hpOn = true, bpOn = false, lpOn = true;
    float hpHz = 20.0f, hpRes = 0.0f, bpHz = 1000.0f, bpWidth = 1.0f, bpRes = 0.0f, lpHz = 20000.0f, lpRes = 0.0f;
};

class MasterFilter
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        for (auto& s : freq) s.setTime (0.03f, sampleRate);
        for (auto& s : res) s.setTime (0.03f, sampleRate);
        for (auto& b : on) b.prepare (sampleRate);
        reset();
    }
    void reset() noexcept
    {
        for (auto& c : f) for (auto& s : c) s.reset();
        first = true;
    }
    void setParams (const MasterFilterParams& p) noexcept
    {
        const float half = 1.0f / 6.0f + p.bpWidth * (5.0f - 1.0f / 6.0f);
        const float c = std::log2 (std::clamp (p.bpHz, 20.0f, 20000.0f));
        const std::array<float, 4> fr { std::log2 (p.hpHz), c - half, c + half, std::log2 (p.lpHz) };
        const std::array<float, 4> rs { p.hpRes, p.bpRes, p.bpRes, p.lpRes };
        for (size_t i = 0; i < 4; ++i) { freq[i].target = fr[i]; res[i].target = rs[i]; }
        on[0].set (p.hpOn); on[1].set (p.bpOn); on[2].set (p.lpOn);
        if (first)
        {
            for (size_t i = 0; i < 4; ++i) { freq[i].reset (fr[i]); res[i].reset (rs[i]); }
            on[0].reset (p.hpOn); on[1].reset (p.bpOn); on[2].reset (p.lpOn);
            first = false;
        }
    }
    void process (float* l, float* r, int n) noexcept
    {
        float* io[2] { l, r };
        for (int i = 0; i < n; ++i)
        {
            for (size_t k = 0; k < 4; ++k) { freq[k].next(); res[k].next(); }
            if (counter-- <= 0)
            {
                counter = 3;
                for (size_t k = 0; k < 4; ++k)
                {
                    const float hz = std::clamp (std::exp2 (freq[k].value), 10.0f, std::min (20000.0f, 0.45f * sr));
                    q[k] = 0.707f + 11.3f * res[k].value * res[k].value;
                    hzs[k] = hz;
                    for (auto& c : f) c[k].set (hz, q[k], sr);
                }
                // BP: compensate the level lost at the centre when the two edges are close.
                const float centre = std::sqrt (hzs[1] * hzs[2]);
                const float r = centre / hzs[1], u = centre / hzs[2];
                const float hp = r * r / std::sqrt ((1.0f - r * r) * (1.0f - r * r) + r * r / (q[1] * q[1]));
                const float lpm = 1.0f / std::sqrt ((1.0f - u * u) * (1.0f - u * u) + u * u / (q[2] * q[2]));
                bpGain = std::min (4.0f, 1.0f / std::max (1.0e-3f, hp * lpm));
            }
            const float g0 = on[0].next(), g1 = on[1].next(), g2 = on[2].next();
            for (size_t c = 0; c < 2; ++c)
            {
                float x = io[c][i];
                auto& s = f[c];
                if (g0 > 0.0f) { s[0].process (x); x += g0 * (s[0].hp - x); }
                if (g1 > 0.0f) { s[1].process (x); const float h = s[1].hp; s[2].process (h); x += g1 * (bpGain * s[2].lp - x); }
                if (g2 > 0.0f) { s[3].process (x); x += g2 * (s[3].lp - x); }
                io[c][i] = x;
            }
        }
    }

private:
    std::array<std::array<Svf, 4>, 2> f;   // HP, BP-low edge (HP), BP-high edge (LP), LP
    std::array<Smoothed, 4> freq, res;
    std::array<Bypass, 3> on;
    std::array<float, 4> q {}, hzs {};
    float sr = 48000.0f, bpGain = 1.0f;
    int counter = 0;
    bool first = true;
};
} // namespace chroma::fx
