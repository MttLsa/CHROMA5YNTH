#pragma once
#include "FxUtil.h"

namespace chroma::fx
{
// Algorithmic reverb shared by Layer Reverb (§2, FDN Hall) and Master Reverb (§4).
// Room / Hall / Large Hall: 16-line feedback delay network, Hadamard mixing, per-line RT60 gains,
// damping and slow modulation, input diffusion all-passes. Plate: figure-of-eight tank plate
// topology (Dattorro, 1997). No convolution.
enum class ReverbType { Room, Plate, Hall, LargeHall };

struct ReverbParams
{
    ReverbType type = ReverbType::Hall;
    float size = 0.5f, decay = 2.0f, predelayMs = 10.0f, damping = 0.4f, diffusion = 0.7f;
    float modulation = 0.2f, lowCut = 20.0f, highCut = 12000.0f, width = 1.0f, mix = 0.25f;
};

class Reverb
{
public:
    static constexpr int numLines = 16;

    void prepare (double sampleRate, bool allowLargeHall = true)
    {
        sr = (float) sampleRate;
        maxScale = allowLargeHall ? 1.6f * 1.5f : 1.5f;
        for (auto& d : fdn) d.prepare ((int) (0.0937f * maxScale * sr * 1.02f) + 64);
        pre.prepare ((int) (0.26f * sr) + 8);
        for (size_t i = 0; i < diff.size(); ++i) diff[i].line.prepare ((int) (diffMs[i % 4] * 0.001f * sr) + 8);
        const float ps = sr / 29761.0f * 1.45f;
        for (size_t i = 0; i < 4; ++i) plateIn[i].line.prepare ((int) (plateInLen[i] * ps) + 8);
        for (size_t i = 0; i < 2; ++i)
        {
            tank[i].ap1.line.prepare ((int) ((plateAp1[i] + 40.0f) * ps) + 8);
            tank[i].d1.prepare ((int) (plateD1[i] * ps) + 8);
            tank[i].ap2.line.prepare ((int) (plateAp2[i] * ps) + 8);
            tank[i].d2.prepare ((int) (plateD2[i] * ps) + 8);
        }
        for (auto* s : { &scale, &preS, &mixS, &widthS }) s->setTime (0.15f, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        for (auto& d : fdn) d.clear();
        for (auto& f : damp) f.reset();
        pre.clear();
        for (auto& a : diff) a.line.clear();
        for (auto& a : plateIn) a.line.clear();
        for (auto& t : tank) { t.ap1.line.clear(); t.d1.clear(); t.ap2.line.clear(); t.d2.clear(); t.lp = 0.0f; }
        bandwidth = 0.0f;
        for (auto& f : lowCut) f.reset();
        for (auto& f : highCut) f.reset();
        for (size_t i = 0; i < osc.size(); ++i) { osc[i] = { std::cos (1.3f * (float) i), std::sin (1.3f * (float) i) }; }
        first = true;
    }

    void setParams (const ReverbParams& p) noexcept
    {
        type = p.type;
        const float typeScale = p.type == ReverbType::Room ? 0.35f : p.type == ReverbType::LargeHall ? 1.6f : 1.0f;
        float sc = typeScale * (0.5f + p.size);
        if (p.type == ReverbType::Plate) sc = 0.55f + 0.9f * p.size;
        scale.target = std::min (sc, p.type == ReverbType::Plate ? 1.45f : maxScale);
        preS.target = std::clamp (p.predelayMs, 0.0f, 250.0f) * 0.001f * sr;
        mixS.target = p.mix; widthS.target = p.width;
        rt60 = std::max (0.05f, p.decay);
        diffusion = p.diffusion;
        modDepth = p.modulation;
        const float dampHz = 18000.0f * std::pow (1.5f / 18.0f, p.damping);
        for (auto& f : damp) f.set (dampHz, sr);
        plateDamp = std::clamp (p.damping * 0.85f, 0.0f, 0.9f);
        for (auto& f : lowCut) f.set (p.lowCut, 0.707f, sr);
        for (auto& f : highCut) f.set (p.highCut, 0.707f, sr);
        if (first) { for (auto* s : { &scale, &preS, &mixS, &widthS }) s->reset (s->target); first = false; }
        updateGains (scale.target);
    }

    void process (float* l, float* r, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float sc = scale.next(), pd = preS.next(), m = mixS.next(), w = widthS.next();
            if ((gainCounter -= 1) <= 0) { gainCounter = 64; updateGains (sc); }
            const float inL = l[i], inR = r[i];
            pre.push (0.5f * (inL + inR));
            preSide = 0.5f * (inL - inR);
            const float x = pd < 1.0f ? 0.5f * (inL + inR) : pre.read (pd);

            float a, b;
            if (type == ReverbType::Plate) plate (x, sc, a, b);
            else fdnTick (x, sc, a, b);

            highCut[0].process (a); a = highCut[0].lp;
            highCut[1].process (b); b = highCut[1].lp;
            lowCut[0].process (a); a = lowCut[0].hp;
            lowCut[1].process (b); b = lowCut[1].hp;

            const float mid = 0.5f * (a + b), side = 0.5f * (a - b) * w;
            l[i] = inL + m * (mid + side - inL);
            r[i] = inR + m * (mid - side - inR);
        }
    }

private:
    struct Allpass
    {
        DelayLine line;
        float process (float x, float delay, float g) noexcept
        {
            const float d = line.read (delay - 1.0f);
            const float v = x - g * d;
            line.push (flushDenormal (v));
            return d + g * v;
        }
    };
    struct Tank { Allpass ap1, ap2; DelayLine d1, d2; float lp = 0.0f; };

    static constexpr float fdnMs[numLines] { 23.1f, 26.3f, 29.9f, 33.7f, 37.1f, 41.3f, 45.7f, 49.9f,
                                             53.3f, 58.9f, 63.1f, 68.7f, 73.3f, 79.9f, 86.1f, 93.7f };
    static constexpr float diffMs[4] { 4.71f, 3.59f, 12.73f, 9.31f };
    static constexpr float plateInLen[4] { 142.0f, 107.0f, 379.0f, 277.0f };
    static constexpr float plateAp1[2] { 672.0f, 908.0f }, plateD1[2] { 4453.0f, 4217.0f };
    static constexpr float plateAp2[2] { 1800.0f, 2656.0f }, plateD2[2] { 3720.0f, 3163.0f };

    void updateGains (float sc) noexcept
    {
        for (int i = 0; i < numLines; ++i)
            fdnGain[(size_t) i] = std::pow (10.0f, -3.0f * fdnMs[i] * 0.001f * sc / rt60);
        const float branch = (plateAp1[0] + plateD1[0] + plateAp2[0] + plateD2[0]) / 29761.0f * sc;
        plateDecay = std::min (0.9995f, std::pow (10.0f, -1.5f * branch / rt60));   // two decay gains per branch
    }

    void fdnTick (float x, float sc, float& outL, float& outR) noexcept
    {
        // Input diffusion (two stereo pairs of all-passes on the mono/side input).
        const float g = 0.7f * diffusion;
        float inA = x, inB = preSide * 0.5f + x;
        for (size_t k = 0; k < 2; ++k)
        {
            inA = diff[k].process (inA, diffMs[k] * 0.001f * sr, g);
            inB = diff[k + 2].process (inB, diffMs[k + 2] * 0.001f * sr, g);
        }

        // Slow modulation: 4 rotating phasors at different rates.
        static constexpr float rates[4] { 0.31f, 0.47f, 0.73f, 1.03f };
        for (size_t k = 0; k < 4; ++k)
        {
            const float w = twoPi * rates[k] / sr;
            auto& o = osc[k];
            const float c = o[0] - w * o[1];
            const float s = o[1] + w * c;
            const float norm = 1.5f - 0.5f * (c * c + s * s);
            o = { c * norm, s * norm };
        }
        const float exc = modDepth * 0.0012f * sr;

        std::array<float, numLines> y;
        const float msToS = 0.001f * sr * sc;
        for (int i = 0; i < numLines; ++i)
        {
            const float m = (i & 1 ? 1.0f : -1.0f) * osc[(size_t) (i & 3)][(size_t) ((i >> 2) & 1)] * exc;
            const float v = fdn[(size_t) i].read (fdnMs[i] * msToS + m + 2.0f);
            y[(size_t) i] = damp[(size_t) i].lp (v) * fdnGain[(size_t) i];
        }
        outL = 0.0f; outR = 0.0f;
        for (int i = 0; i < numLines; i += 2)
        {
            const float s = (i & 2) ? -1.0f : 1.0f;
            outL += s * y[(size_t) i];
            outR += s * y[(size_t) i + 1];
        }
        outL *= 0.5f; outR *= 0.5f;

        // Fast Walsh-Hadamard transform (orthogonal after the 1/4 scaling).
        for (int len = 1; len < numLines; len <<= 1)
            for (int i = 0; i < numLines; i += len << 1)
                for (int j = i; j < i + len; ++j)
                {
                    const float u = y[(size_t) j], v = y[(size_t) (j + len)];
                    y[(size_t) j] = u + v;
                    y[(size_t) (j + len)] = u - v;
                }
        for (int i = 0; i < numLines; ++i)
        {
            const float in = (i & 1) ? inB : inA;
            fdn[(size_t) i].push (flushDenormal (0.25f * y[(size_t) i] + ((i & 2) ? -in : in) * 0.5f));
        }
    }

    void plate (float x, float sc, float& outL, float& outR) noexcept
    {
        const float k = sr / 29761.0f;
        const float ks = k * sc;
        bandwidth += 0.9995f * (x - bandwidth);
        float v = bandwidth;
        const float gi1 = 0.75f * diffusion, gi2 = 0.625f * diffusion;
        v = plateIn[0].process (v, plateInLen[0] * k, gi1);
        v = plateIn[1].process (v, plateInLen[1] * k, gi1);
        v = plateIn[2].process (v, plateInLen[2] * k, gi2);
        v = plateIn[3].process (v, plateInLen[3] * k, gi2);

        auto& o = osc[0];
        { const float w = twoPi * 1.0f / sr; const float c = o[0] - w * o[1]; const float s = o[1] + w * c;
          const float nn = 1.5f - 0.5f * (c * c + s * s); o = { c * nn, s * nn }; }
        const float exc = 16.0f * k * (0.25f + 1.5f * modDepth);
        const float dd2 = std::clamp (plateDecay + 0.15f, 0.25f, 0.5f);

        const float fbL = tank[1].d2.tap ((int) (plateD2[1] * ks) - 1) * plateDecay;
        const float fbR = tank[0].d2.tap ((int) (plateD2[0] * ks) - 1) * plateDecay;
        float in[2] { v + fbL, v + fbR };
        for (size_t t = 0; t < 2; ++t)
        {
            auto& T = tank[t];
            float s = T.ap1.process (in[t], plateAp1[t] * ks + exc * o[t], -0.7f);
            T.d1.push (s);
            s = T.d1.tap ((int) (plateD1[t] * ks) - 1);
            T.lp = flushDenormal (s + plateDamp * (T.lp - s));
            s = T.lp * plateDecay;
            s = T.ap2.process (s, plateAp2[t] * ks, dd2);
            T.d2.push (s);
        }

        auto tp = [ks] (const DelayLine& d, float n) { return d.tap ((int) (n * ks)); };
        const auto& L = tank[0]; const auto& R = tank[1];
        outL = tp (R.d1, 266) + tp (R.d1, 2974) - tp (R.ap2.line, 1913) + tp (R.d2, 1996)
             - tp (L.d1, 1990) - tp (L.ap2.line, 187) - tp (L.d2, 1066);
        outR = tp (L.d1, 353) + tp (L.d1, 3627) - tp (L.ap2.line, 1228) + tp (L.d2, 2673)
             - tp (R.d1, 2111) - tp (R.ap2.line, 335) - tp (R.d2, 121);
        outL *= 0.6f; outR *= 0.6f;
    }

    std::array<DelayLine, numLines> fdn;
    std::array<OnePole, numLines> damp;
    std::array<float, numLines> fdnGain {};
    std::array<Allpass, 4> diff, plateIn;
    std::array<Tank, 2> tank;
    std::array<std::array<float, 2>, 4> osc {};
    std::array<Svf, 2> lowCut, highCut;
    DelayLine pre;
    Smoothed scale, preS, mixS, widthS;
    ReverbType type = ReverbType::Hall;
    float sr = 48000.0f, maxScale = 2.4f, rt60 = 2.0f, diffusion = 0.7f, modDepth = 0.2f;
    float plateDecay = 0.5f, plateDamp = 0.3f, bandwidth = 0.0f, preSide = 0.0f;
    int gainCounter = 0;
    bool first = true;
};
} // namespace chroma::fx
