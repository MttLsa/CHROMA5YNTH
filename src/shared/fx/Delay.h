#pragma once
#include "FxUtil.h"

namespace chroma::fx
{
// Shared delay for Layer Delay (§2: Digital with optional Ping-Pong) and Master Delay (§4).
enum class DelayType { Digital, Tape, Bbd, PingPong, DualLR, Multitap };

struct DelayParams
{
    DelayType type = DelayType::Digital;
    float timeL = 0.375f, timeR = 0.375f;     // seconds (already resolved from Sync)
    float feedback = 0.35f, hpHz = 20.0f, lpHz = 12000.0f, width = 1.0f;
    float modulation = 0.0f, ducking = 0.0f, mix = 0.3f;
};

class Delay
{
public:
    static constexpr float maxTime = 2.0f;

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        for (auto& d : lines) d.prepare ((int) ((maxTime * 1.02f) * sr) + 16);
        for (auto* s : { &tL, &tR }) s->setTime (0.12f, sampleRate);
        for (auto* s : { &fbS, &widthS, &modS, &duckS, &mixS }) s->setTime (0.03f, sampleRate);
        envAtt = (float) std::exp (-1.0 / (0.005 * sampleRate));
        envRel = (float) std::exp (-1.0 / (0.25 * sampleRate));
        reset();
    }

    void reset() noexcept
    {
        for (auto& d : lines) d.clear();
        for (auto& f : hp) f.reset();
        for (auto& f : lp) f.reset();
        y = { 0.0f, 0.0f };
        env = 0.0f;
        wow.reset (0.0); flutter.reset (0.3); mod.reset (0.6);
        first = true;
    }

    void setParams (const DelayParams& p) noexcept
    {
        type = p.type;
        const bool dual = p.type == DelayType::DualLR || p.type == DelayType::Digital;
        tL.target = std::clamp (p.timeL, 0.001f, maxTime) * sr;
        tR.target = std::clamp (dual ? p.timeR : p.timeL, 0.001f, maxTime) * sr;
        fbS.target = std::clamp (p.feedback, 0.0f, 0.98f);
        widthS.target = p.width; modS.target = p.modulation; duckS.target = p.ducking; mixS.target = p.mix;
        float lpHz = p.lpHz;
        if (p.type == DelayType::Bbd) lpHz = std::min (lpHz, 4500.0f);
        if (p.type == DelayType::Tape) lpHz = std::min (lpHz, 9000.0f);
        for (auto& f : hp) f.set (p.hpHz, sr);
        for (auto& f : lp) f.set (lpHz, sr);
        if (first)
        {
            for (auto* s : { &tL, &tR, &fbS, &widthS, &modS, &duckS, &mixS }) s->reset (s->target);
            first = false;
        }
    }

    void process (float* l, float* r, int n) noexcept
    {
        const bool tape = type == DelayType::Tape, bbd = type == DelayType::Bbd;
        for (int i = 0; i < n; ++i)
        {
            const float dl = tL.next(), dr = tR.next(), fb = fbS.next(), w = widthS.next(), md = modS.next();
            const float duck = duckS.next(), m = mixS.next();
            const float inL = l[i], inR = r[i];

            // Time modulation: user Modulation + tape wow/flutter or BBD clock wobble.
            wow.advance (0.6f, sr); flutter.advance (6.5f, sr); mod.advance (0.9f, sr);
            float rel = 0.004f * md * FxLfo::sine (mod.phase);
            if (tape) rel += 0.0025f * FxLfo::sine (wow.phase) + 0.0004f * FxLfo::sine (flutter.phase);
            if (bbd) rel += 0.0012f * FxLfo::tri (wow.phase);
            const float readL = dl * (1.0f + rel), readR = dr * (1.0f + rel);

            float a, b;
            if (type == DelayType::Multitap)
            {
                const auto& L = lines[0];
                const float t1 = L.read (0.25f * readL), t2 = L.read (0.5f * readL), t3 = L.read (0.75f * readL), t4 = L.read (readL);
                y[0] = t4;
                a = 0.55f * t1 + 0.75f * t3 + 0.7f * t4;
                b = 0.65f * t2 + 0.7f * t4 + 0.2f * t1;
            }
            else
            {
                a = lines[0].read (readL);
                b = lines[1].read (readR);
                y = { a, b };
            }

            // Feedback path: HP -> LP (-> saturation for Tape/BBD).
            auto loop = [&] (int c, float x)
            {
                float v = lp[(size_t) c].lp (hp[(size_t) c].hp (x)) * fb;
                if (tape) v = std::tanh (v * 1.3f) / 1.3f;
                else if (bbd) v = std::tanh (v);
                return flushDenormal (v);
            };

            switch (type)
            {
                case DelayType::PingPong:
                    lines[0].push (0.5f * (inL + inR) + loop (1, b));
                    lines[1].push (loop (0, a));
                    break;
                case DelayType::Multitap:
                    lines[0].push (0.5f * (inL + inR) + loop (0, y[0]));
                    break;
                case DelayType::Digital: case DelayType::Tape: case DelayType::Bbd: case DelayType::DualLR:
                    if (pingPong)
                    {
                        lines[0].push (0.5f * (inL + inR) + loop (1, b));
                        lines[1].push (loop (0, a));
                    }
                    else
                    {
                        lines[0].push (inL + loop (0, a));
                        lines[1].push (inR + loop (1, b));
                    }
                    break;
            }

            // Wet: width (M/S) and ducking from the input level.
            const float mid = 0.5f * (a + b), side = 0.5f * (a - b) * w;
            const float lvl = std::max (std::fabs (inL), std::fabs (inR));
            env = lvl > env ? lvl + (env - lvl) * envAtt : lvl + (env - lvl) * envRel;
            const float duckGain = 1.0f - duck * 0.937f * std::min (1.0f, env * 4.0f);
            const float wl = (mid + side) * duckGain, wr = (mid - side) * duckGain;
            l[i] = inL + m * (wl - inL);
            r[i] = inR + m * (wr - inR);
        }
    }

    bool pingPong = false;   // Layer Delay "Ping-Pong" switch on the Digital type

private:
    std::array<DelayLine, 2> lines;
    std::array<OnePole, 2> hp, lp;
    std::array<float, 2> y {};
    FxLfo wow, flutter, mod;
    Smoothed tL, tR, fbS, widthS, modS, duckS, mixS;
    DelayType type = DelayType::Digital;
    float sr = 48000.0f, env = 0.0f, envAtt = 0.0f, envRel = 0.0f;
    bool first = true;
};
} // namespace chroma::fx
