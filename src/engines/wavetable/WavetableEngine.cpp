#include "WavetableEngine.h"
#include "engines/EngineParams.h"
#include <algorithm>
#include <cmath>

namespace chroma
{
namespace
{
constexpr double twoPi = 6.283185307179586;
constexpr float maxInc = 0.45f;

inline double wrap (double p) noexcept { return p - std::floor (p); }

inline void panGains (float pan, float& gl, float& gr) noexcept
{
    const float angle = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.25f * 3.14159265f;
    gl = std::cos (angle) * 1.41421356f;
    gr = std::sin (angle) * 1.41421356f;
}

inline uint32_t lcg (uint32_t& s) noexcept { s = s * 1664525u + 1013904223u; return s; }
inline double rand01 (uint32_t& s) noexcept { return (double) (lcg (s) >> 8) / 16777216.0; }
inline float white (uint32_t& s) noexcept { return (float) (lcg (s) >> 8) * (2.0f / 16777216.0f) - 1.0f; }

inline float noteHz (float pitch) noexcept { return 440.0f * std::exp2 ((pitch - 69.0f) / 12.0f); }

// Ping-pong fold into [0, 1].
inline float reflect01 (double u) noexcept
{
    u -= 2.0 * std::floor (u * 0.5);
    return (float) (u > 1.0 ? 2.0 - u : u);
}

inline float readTable (const float* t, int len, double p) noexcept
{
    const double x = p * len;
    int i = (int) x;
    i = std::clamp (i, 0, len - 1);
    const float f = (float) (x - i);
    const float y0 = t[i - 1], y1 = t[i], y2 = t[i + 1], y3 = t[i + 2];   // 4-point Hermite
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

inline double bend (double p, double k) noexcept { return p * k / (1.0 + (k - 1.0) * p); }

const std::array<float, wt::specSize>& logK()
{
    static const std::array<float, wt::specSize> t = []
    {
        std::array<float, wt::specSize> a {};
        for (int k = 1; k < wt::specSize; ++k) a[(size_t) k] = std::log ((float) k);
        return a;
    }();
    return t;
}

inline std::complex<float> unit (std::complex<float> c) noexcept
{
    const float m = std::abs (c);
    return m > 1.0e-12f ? c / m : std::complex<float> (0.0f, -1.0f);
}
} // namespace

double WavetableEngine::warpPhase (int mode, double p, float a, float mod, bool& silent) noexcept
{
    switch (mode)
    {
        case BendPlus:  return bend (p, std::exp (3.0 * a));
        case BendMinus: return bend (p, std::exp (-3.0 * a));
        case BendBoth:
        {
            const double k = std::exp (3.0 * a);
            return p < 0.5 ? 0.5 * bend (2.0 * p, k) : 1.0 - 0.5 * bend (2.0 - 2.0 * p, k);
        }
        case Pwm:
        {
            const double w = 1.0 - 0.95 * a;
            if (p < w) return p / w;
            silent = true;
            return 0.0;
        }
        case Sync: return wrap (p * (1.0 + 15.0 * a));
        case Asym:
        {
            const double c = 0.5 - 0.49 * a;
            return p < c ? 0.5 * p / c : 0.5 + 0.5 * (p - c) / (1.0 - c);
        }
        case Quantize:
        {
            if (a <= 0.0f) return p;
            const double n = std::exp2 (10.0 - 9.0 * a);
            return std::floor (p * n) / n;
        }
        case Fm: return wrap (p + 2.0 * a * mod);
        default: return p;   // Mirror (two reads), AM / Ring (post) handled by the caller
    }
}

// ------------------------------------------------------------------ lifecycle
WavetableEngine::WavetableEngine()
{
    (void) wt::factory();   // build the factory bank off the audio thread
    (void) logK();
}

void WavetableEngine::prepare (double sampleRate, int maxBlockSize, int maxVoices)
{
    sr = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    maxN = maxBlock * maxOversampling;
    const auto nv = (size_t) std::max (1, maxVoices);
    voices.assign (nv, VoiceState {});
    dyn.assign (nv * numOsc, Dyn {});
    prevRaw.assign (nv * numOsc * (size_t) maxN, 0.0f);
    for (int k = 0; k < numOsc; ++k)
        for (auto* b : { &oscL[(size_t) k], &oscR[(size_t) k], &oscRaw[(size_t) k] })
            b->assign ((size_t) maxN + 8, 0.0f);
    for (auto* b : { &subBuf, &busL, &busR })
        b->assign ((size_t) maxN + 8, 0.0f);
    work.assign ((size_t) Decimator::workSize (maxN) + 8, 0.0f);
    fftWork.assign ((size_t) dynLen * 2 + 8, 0.0f);
    specA.assign ((size_t) wt::specSize, {});
    specB.assign ((size_t) wt::specSize, {});
    mags.assign ((size_t) wt::specSize, 0.0f);
    tmpMags.assign ((size_t) wt::specSize, 0.0f);
    (void) Halfband::longStage();
    (void) Halfband::shortStage();
    reset();
}

void WavetableEngine::reset()
{
    for (size_t v = 0; v < voices.size(); ++v)
        kill ((int) v);
}

void WavetableEngine::kill (int voice)
{
    auto& vs = voices[(size_t) voice];
    const uint32_t seed = vs.seed;
    vs = VoiceState {};
    vs.seed = seed == 1 ? 0x9E3779B9u * (uint32_t) (voice + 1) : seed;
    vs.rngL = 0x2345671u + (uint32_t) voice * 7919u;
    vs.rngR = 0x6543217u + (uint32_t) voice * 104729u;
    for (int k = 0; k < numOsc; ++k)
    {
        auto& d = dyn[(size_t) (voice * numOsc + k)];
        d.valid = d.haveFrozen = false;
    }
}

void WavetableEngine::setOversampling (int factor)
{
    const int f = factor >= 8 ? 8 : factor >= 4 ? 4 : factor >= 2 ? 2 : 1;
    if (f == os)
        return;
    os = f;
    for (auto& vs : voices)
        for (auto& d : vs.dec) d.reset();
}

void WavetableEngine::noteOn (int voice, const EngineNote& n)
{
    if (n.retrigger)
        voices[(size_t) voice].pendingStart = true;
}

const wt::Table* WavetableEngine::tableFor (int choice) const noexcept
{
    const auto& bank = wt::factory();
    const int nf = (int) bank.tables.size();
    if (choice >= 0 && choice < nf)
        return bank.tables[(size_t) choice].get();
    const int u = choice - nf;
    if (users != nullptr && u >= 0 && u < wt::numUserSlots)
        if (const auto* t = users[u].get())
            return t;
    return bank.tables[0].get();
}

// ------------------------------------------------------------------ spectral path
void WavetableEngine::updateDyn (Dyn& d, const wt::Table& tab, float pos, const EngineVoiceParams& p, int k, int H) noexcept
{
    const auto u = (size_t) k;
    const int nf = tab.numFrames;
    constexpr int K = wt::maxHarmonics;
    const auto interp = [&] (float ps, std::complex<float>* out) noexcept
    {
        const float f = std::clamp (ps, 0.0f, 1.0f) * (float) (nf - 1);
        const int i0 = std::min ((int) f, nf - 1);
        const float fr = f - (float) i0;
        const auto* s0 = tab.spectrum (i0);
        const auto* s1 = tab.spectrum (std::min (i0 + 1, nf - 1));
        for (int h = 1; h <= K; ++h) out[h] = s0[h] + fr * (s1[h] - s0[h]);
    };

    auto* A = specA.data();
    auto* B = specB.data();
    float* m = mags.data();
    interp (pos, A);
    float e0 = 0.0f;
    for (int h = 1; h <= K; ++h)
    {
        m[h] = std::abs (A[h]);
        if (h <= H) e0 += m[h] * m[h];
    }

    const float blur = engineParam (p, lp::wtOscBlur[u]);
    if (blur > 1.0e-4f)
    {
        const float w = 0.25f * blur;
        for (float j : { -1.0f, -0.5f, 0.5f, 1.0f })
        {
            interp (reflect01 (pos + j * w), B);
            for (int h = 1; h <= K; ++h) m[h] += std::abs (B[h]);
        }
        for (int h = 1; h <= K; ++h) m[h] *= 0.2f;
    }

    if (! d.haveFrozen)
    {
        std::copy (m, m + wt::specSize, d.frozen.begin());
        d.haveFrozen = true;
    }
    const float freeze = engineParam (p, lp::wtOscFreeze[u]);
    if (freeze > 1.0e-4f)
        for (int h = 1; h <= K; ++h) m[h] += freeze * (d.frozen[(size_t) h] - m[h]);

    const float formant = engineParam (p, lp::wtOscFormant[u]);
    if (std::fabs (formant) > 1.0e-4f)
    {
        const float inv = std::exp2 (-2.0f * formant);
        float* t = tmpMags.data();
        for (int h = 1; h <= K; ++h)
        {
            const float x = (float) h * inv;
            const int i = (int) x;
            if (x < 1.0f || i >= K) { t[h] = 0.0f; continue; }
            t[h] = m[i] + (x - (float) i) * (m[i + 1] - m[i]);
        }
        std::copy (t + 1, t + K + 1, m + 1);
    }

    const float tilt = engineParam (p, lp::wtOscTilt[u]);
    if (std::fabs (tilt) > 1.0e-4f)
    {
        const auto& lk = logK();
        for (int h = 2; h <= K; ++h) m[h] *= std::exp (2.0f * tilt * lk[(size_t) h]);
    }

    std::fill (B, B + wt::specSize, std::complex<float> {});
    const float stretch = engineParam (p, lp::wtOscStretch[u]);
    if (std::fabs (stretch) > 1.0e-4f)
    {
        const float e = 1.0f + 0.5f * stretch;
        const auto& lk = logK();
        for (int h = 1; h <= K; ++h)
        {
            const int j = (int) std::lround (std::exp (e * lk[(size_t) h]));
            if (j >= 1 && j <= H) B[j] += unit (A[h]) * m[h];
        }
    }
    else
    {
        for (int h = 1; h <= H; ++h) B[h] = unit (A[h]) * m[h];
    }

    float e1 = 0.0f;
    for (int h = 1; h <= H; ++h) e1 += std::norm (B[h]);
    if (e1 > 1.0e-20f)
    {
        const float g = std::min (8.0f, std::sqrt (e0 / e1));
        for (int h = 1; h <= H; ++h) B[h] *= g;
    }
    wt::synthesize (B, H, d.b.data() + 1, dynLen, fftWork.data());
}

// ------------------------------------------------------------------ sources
void WavetableEngine::renderSub (VoiceState& vs, const EngineVoiceParams& p, int N, float pitch0, float pitch1) noexcept
{
    const auto& sub = *wt::factory().sub;
    const int wave = std::clamp (engineChoice (p, lp::wtSubWave), 0, 3);
    const float off = 12.0f * (float) engineChoice (p, lp::wtSubOctave) + (p.driftSemis != nullptr ? p.driftSemis[0] : 0.0f);
    const double rate = sr * os;
    const float i0 = std::min (noteHz (pitch0 + off) / (float) rate, maxInc);
    const float i1 = std::min (noteHz (pitch1 + off) / (float) rate, maxInc);
    const int lvl = wt::levelFor (std::max (i0, i1) * (float) os);
    const float* t = sub.level (wave, lvl);
    const int len = wt::levelLength (lvl);
    const float di = (i1 - i0) / (float) N;
    double ph = vs.subPhase;
    for (int i = 0; i < N; ++i)
    {
        ph = wrap (ph + i0 + di * (float) (i + 1));
        subBuf[(size_t) i] = readTable (t, len, ph);
    }
    vs.subPhase = ph;
}

void WavetableEngine::renderOsc (int voice, VoiceState& vs, int k, const EngineVoiceParams& p, int n,
                                 float pitch0, float pitch1, const float* mod, float* L, float* R, float* raw) noexcept
{
    const auto u = (size_t) k;
    auto& o = vs.osc[u];
    const int N = n * os;
    const bool on = engineSwitch (p, lp::wtOscOn[u]);
    const wt::Table& tab = *tableFor (engineChoice (p, lp::wtOscTable[u]));
    const int nf = tab.numFrames;

    // position: base + scan (ping-pong), ramped across the block
    const float scan = engineParam (p, lp::wtOscScan[u]);
    o.scan += 4.0 * scan * std::fabs (scan) * (double) n / sr;
    o.scan -= 2.0 * std::floor (o.scan * 0.5);
    const float posT = reflect01 ((double) engineParam (p, lp::wtOscPos[u]) + o.scan);
    if (! o.havePos) { o.pos = posT; o.havePos = true; }
    const float pos0 = o.pos, pos1 = posT;
    o.pos = posT;

    // unison lanes
    const int U = std::clamp (engineChoice (p, lp::wtOscUnison[u]), 1, maxUnison);
    const float detC = engineParam (p, lp::wtOscUniDetune[u]);
    const float spread = engineParam (p, lp::wtOscUniSpread[u]);
    const float offset = 12.0f * (float) engineChoice (p, lp::wtOscOctave[u]) + (float) engineChoice (p, lp::wtOscSemi[u])
                       + engineParam (p, lp::wtOscFine[u]) * 0.01f;
    const float detHz = engineParam (p, lp::wtOscDetune[u]);
    const auto rate = (float) (sr * os);
    const float f0a = noteHz (pitch0 + offset), f0b = noteHz (pitch1 + offset);
    const float uNorm = 1.0f / std::sqrt ((float) U);
    std::array<float, maxUnison> inc0 {}, inc1 {}, gl {}, gr {};
    float maxCps = 0.0f;
    for (int j = 0; j < U; ++j)
    {
        const float posU = U > 1 ? (float) j / (float) (U - 1) - 0.5f : 0.0f;
        float cents = posU * detC;
        if (p.driftSemis != nullptr)
            cents += p.driftSemis[(size_t) ((j + 5 * k) % maxUnison)] * 100.0f;
        const float ratio = std::exp2 (cents / 1200.0f);
        inc0[(size_t) j] = std::clamp ((f0a * ratio + detHz) / rate, -maxInc, maxInc);
        inc1[(size_t) j] = std::clamp ((f0b * ratio + detHz) / rate, -maxInc, maxInc);
        maxCps = std::max ({ maxCps, std::fabs (inc0[(size_t) j]), std::fabs (inc1[(size_t) j]) });
        panGains (2.0f * posU * spread, gl[(size_t) j], gr[(size_t) j]);
        gl[(size_t) j] *= uNorm;
        gr[(size_t) j] *= uNorm;
    }

    const int warp = std::clamp (engineChoice (p, lp::wtOscWarp[u]), 0, (int) Ring);
    const float amt = engineParam (p, lp::wtOscWarpAmount[u]);
    const float syncRatio = warp == Sync ? 1.0f + 15.0f * amt : 1.0f;
    const int lvl = wt::levelFor (maxCps * (float) os * syncRatio);
    const int len = wt::levelLength (lvl);

    const float pd = engineParam (p, lp::wtOscPd[u]) * 0.95f / (float) twoPi;
    const float bits = engineParam (p, lp::wtOscBits[u]);
    const float q = bits < 15.99f ? std::exp2 (bits - 1.0f) : 0.0f;
    const float srr = engineParam (p, lp::wtOscSrr[u]);
    const float holdLen = (1.0f + 63.0f * srr) * (float) os;
    const float grain = engineParam (p, lp::wtOscGrain[u]);
    const double dGrain = 1.0 / std::max (1.0, (double) engineParam (p, lp::wtOscGrainSize[u]) * 0.001 * rate);

    // spectral path
    const bool spectral = std::fabs (engineParam (p, lp::wtOscFreeze[u])) > 1.0e-4f || engineParam (p, lp::wtOscBlur[u]) > 1.0e-4f
                       || std::fabs (engineParam (p, lp::wtOscTilt[u])) > 1.0e-4f
                       || std::fabs (engineParam (p, lp::wtOscFormant[u])) > 1.0e-4f
                       || std::fabs (engineParam (p, lp::wtOscStretch[u])) > 1.0e-4f;
    Dyn& dy = dyn[(size_t) (voice * numOsc + k)];
    Dyn* d = spectral ? &dy : nullptr;
    if (d != nullptr)
    {
        const int H = std::min (dynHarmonics, wt::levelHarmonics (lvl));
        const float dPos = reflect01 ((double) pos0 + (grain > 0.0f ? (o.grainPhase < 0.5 ? o.offA : o.offB) : 0.0f));
        if (! d->valid)
        {
            updateDyn (*d, tab, dPos, p, k, H);
            d->a = d->b;
            d->xf = 1.0f;
            d->countdown = dynUpdate;
            d->valid = true;
        }
        else if (d->xf >= 1.0f && d->countdown <= 0)
        {
            d->a = d->b;
            updateDyn (*d, tab, dPos, p, k, H);
            d->xf = 0.0f;
            d->countdown = dynUpdate;
        }
        d->countdown -= n;
    }
    else
    {
        dy.valid = false;
    }
    const float dxf = 1.0f / (float) (dynUpdate * os);

    const float invN = 1.0f / (float) N;
    for (int i = 0; i < N; ++i)
    {
        const float a = (float) (i + 1) * invN;
        const float pos = pos0 + (pos1 - pos0) * a;
        float wA = 1.0f, posA = pos, posB = pos;
        if (grain > 0.0f)
        {
            double g = o.grainPhase + dGrain;
            if (g >= 1.0) { g -= 1.0; o.offA = (float) (rand01 (vs.seed) * 2.0 - 1.0) * grain; }
            if (o.grainPhase < 0.5 && g >= 0.5) o.offB = (float) (rand01 (vs.seed) * 2.0 - 1.0) * grain;
            o.grainPhase = g;
            wA = 1.0f - std::fabs (2.0f * (float) g - 1.0f);
            posA = reflect01 ((double) pos + o.offA);
            posB = reflect01 ((double) pos + o.offB);
        }
        const float fA = posA * (float) (nf - 1), fB = posB * (float) (nf - 1);
        const int iA = std::min ((int) fA, nf - 1), iB = std::min ((int) fB, nf - 1);
        const float frA = fA - (float) iA, frB = fB - (float) iB;
        const float* tA0 = tab.level (iA, lvl);
        const float* tA1 = tab.level (std::min (iA + 1, nf - 1), lvl);
        const float* tB0 = tab.level (iB, lvl);
        const float* tB1 = tab.level (std::min (iB + 1, nf - 1), lvl);
        float xf = 0.0f;
        if (d != nullptr) { d->xf = std::min (1.0f, d->xf + dxf); xf = d->xf; }

        const auto read = [&] (double ph) noexcept
        {
            if (d != nullptr)
            {
                const float va = readTable (d->a.data() + 1, dynLen, ph);
                return va + xf * (readTable (d->b.data() + 1, dynLen, ph) - va);
            }
            const float a0 = readTable (tA0, len, ph);
            float v = a0 + frA * (readTable (tA1, len, ph) - a0);
            if (grain > 0.0f)
            {
                const float b0 = readTable (tB0, len, ph);
                v = wA * v + (1.0f - wA) * (b0 + frB * (readTable (tB1, len, ph) - b0));
            }
            return v;
        };

        const float mv = mod != nullptr ? mod[i] : 0.0f;
        float sL = 0.0f, sR = 0.0f, sRaw = 0.0f;
        for (int j = 0; j < U; ++j)
        {
            const auto ju = (size_t) j;
            const double ph = wrap (o.phase[ju] + (double) (inc0[ju] + (inc1[ju] - inc0[ju]) * a));
            o.phase[ju] = ph;
            bool silent = false;
            double rp = warpPhase (warp, ph, amt, mv, silent);
            if (silent) continue;
            if (pd > 0.0f) rp = wrap (rp - pd * std::sin (twoPi * rp));
            float v = read (rp);
            if (warp == Mirror && amt > 0.0f)
                v += amt * (read (rp < 0.5 ? 2.0 * rp : 2.0 - 2.0 * rp) - v);
            sRaw += v;
            sL += v * gl[ju];
            sR += v * gr[ju];
        }
        sRaw *= uNorm;
        if (warp == Am)   { const float g = 1.0f - amt * 0.5f * (1.0f - mv); sL *= g; sR *= g; sRaw *= g; }
        if (warp == Ring) { const float g = 1.0f - amt + amt * mv;         sL *= g; sR *= g; sRaw *= g; }
        if (q > 0.0f)
        {
            sL = std::round (sL * q) / q;
            sR = std::round (sR * q) / q;
            sRaw = std::round (sRaw * q) / q;
        }
        if (srr > 0.0f)
        {
            o.holdCount += 1.0f;
            if (o.holdCount >= holdLen) { o.holdCount -= holdLen; o.holdL = sL; o.holdR = sR; o.holdRaw = sRaw; }
            sL = o.holdL; sR = o.holdR; sRaw = o.holdRaw;
        }
        L[i] = sL;
        R[i] = sR;
        raw[i] = sRaw;
    }

    // volume / pan / width (ramped)
    float tl = 0.0f, tr = 0.0f;
    if (on)
    {
        panGains (engineParam (p, lp::wtOscPan[u]), tl, tr);
        const float vol = engineParam (p, lp::wtOscVolume[u]);
        tl *= vol; tr *= vol;
    }
    const float wTarget = engineParam (p, lp::wtOscUniWidth[u]);
    const float dl = (tl - o.gainL) * invN, dr = (tr - o.gainR) * invN, dw = (wTarget - o.width) * invN;
    float gL = o.gainL, gR = o.gainR, w = o.width;
    for (int i = 0; i < N; ++i)
    {
        gL += dl; gR += dr; w += dw;
        const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]) * w;
        L[i] = (m + s) * gL;
        R[i] = (m - s) * gR;
    }
    o.gainL = tl; o.gainR = tr; o.width = wTarget;
}

// ------------------------------------------------------------------ render
bool WavetableEngine::render (int voice, const EngineVoiceParams& p, float* left, float* right, int n) noexcept
{
    auto& vs = voices[(size_t) voice];
    for (int done = 0; done < n;)
    {
        const int m = std::min (maxBlock, n - done);
        const float a0 = (float) done / (float) n, a1 = (float) (done + m) / (float) n;
        const float p0 = p.pitchStart + (p.pitchEnd - p.pitchStart) * a0;
        const float p1 = p.pitchStart + (p.pitchEnd - p.pitchStart) * a1;
        renderBlock (voice, vs, p, left + done, right + done, m, p0, p1);
        done += m;
    }
    return false;
}

bool WavetableEngine::renderBlock (int voice, VoiceState& vs, const EngineVoiceParams& p, float* left, float* right,
                                   int n, float pitch0, float pitch1) noexcept
{
    const int N = n * os;
    float* prev = prevRaw.data() + (size_t) voice * numOsc * (size_t) maxN;

    if (vs.pendingStart)
    {
        vs.pendingStart = false;
        for (int k = 0; k < numOsc; ++k)
        {
            const auto u = (size_t) k;
            auto& o = vs.osc[u];
            const double ph = engineParam (p, lp::wtOscPhase[u]) / 360.0;
            const double ra = engineParam (p, lp::wtOscRandPhase[u]);
            if (engineSwitch (p, lp::wtOscReset[u]))
                for (auto& x : o.phase) x = wrap (ph + ra * rand01 (vs.seed));
            o.scan = 0.0;
            o.havePos = false;
            o.grainPhase = 0.0;
            o.offA = o.offB = 0.0f;
            o.holdCount = 0.0f;
            auto& d = dyn[(size_t) (voice * numOsc + k)];
            d.valid = d.haveFrozen = false;
        }
        std::fill (prev, prev + numOsc * maxN, 0.0f);
        vs.subPhase = 0.0;
    }

    // which sources run
    std::array<bool, numOsc> on {}, run {};
    std::array<int, numOsc> warp {}, src {};
    bool subAsSource = false;
    for (int k = 0; k < numOsc; ++k)
    {
        const auto u = (size_t) k;
        on[u] = engineSwitch (p, lp::wtOscOn[u]);
        run[u] = on[u] || vs.osc[u].gainL != 0.0f || vs.osc[u].gainR != 0.0f;
        warp[u] = engineChoice (p, lp::wtOscWarp[u]);
        src[u] = std::clamp (engineChoice (p, lp::wtOscWarpSrc[u]), 0, 3);
        if (run[u] && (warp[u] == Fm || warp[u] == Am || warp[u] == Ring) && src[u] == 3)
            subAsSource = true;
    }
    const bool subOn = engineSwitch (p, lp::wtSubOn);
    float subTL = 0.0f, subTR = 0.0f;
    if (subOn)
    {
        panGains (engineParam (p, lp::wtSubPan), subTL, subTR);
        const float lv = engineParam (p, lp::wtSubLevel);
        subTL *= lv; subTR *= lv;
    }
    const bool subRun = subOn || subAsSource || vs.subGainL != 0.0f || vs.subGainR != 0.0f;
    if (subRun)
    {
        if (! vs.subActive) vs.subPhase = 0.0;
        renderSub (vs, p, N, pitch0, pitch1);
    }
    vs.subActive = subRun;

    float* bl = busL.data();
    float* br = busR.data();
    std::fill (bl, bl + N, 0.0f);
    std::fill (br, br + N, 0.0f);
    bool any = false;

    for (int k = 0; k < numOsc; ++k)
    {
        const auto u = (size_t) k;
        auto& o = vs.osc[u];
        if (! run[u]) { o.active = false; continue; }
        const float* mod = nullptr;
        if (warp[u] == Fm || warp[u] == Am || warp[u] == Ring)
        {
            const int s = src[u];
            if (s == 3) mod = subBuf.data();
            else if (on[(size_t) s]) mod = s < k ? oscRaw[(size_t) s].data() : prev + (size_t) s * (size_t) maxN;
        }
        o.active = true;
        float* L = oscL[u].data();
        float* R = oscR[u].data();
        float* raw = oscRaw[u].data();
        renderOsc (voice, vs, k, p, n, pitch0, pitch1, mod, L, R, raw);
        std::copy (raw, raw + N, prev + (size_t) k * (size_t) maxN);
        for (int i = 0; i < N; ++i) { bl[i] += L[i]; br[i] += R[i]; }
        any = true;
    }

    if (subRun && (subOn || vs.subGainL != 0.0f || vs.subGainR != 0.0f))
    {
        const float* s = subBuf.data();
        const float dl = (subTL - vs.subGainL) / (float) N, dr = (subTR - vs.subGainR) / (float) N;
        float gL = vs.subGainL, gR = vs.subGainR;
        for (int i = 0; i < N; ++i)
        {
            gL += dl; gR += dr;
            bl[i] += s[i] * gL;
            br[i] += s[i] * gR;
        }
        vs.subGainL = subTL; vs.subGainR = subTR;
        any = true;
    }

    if (any) vs.hold = 256;
    else if (vs.hold > 0) vs.hold = std::max (0, vs.hold - n);
    if (any || vs.hold > 0)
    {
        vs.dec[0].process (bl, left, n, os, work.data());
        vs.dec[1].process (br, right, n, os, work.data());
    }
    else
    {
        std::fill (left, left + n, 0.0f);
        std::fill (right, right + n, 0.0f);
    }

    // noise (base rate)
    const float nl = engineParam (p, lp::wtNoiseLevel);
    if (nl > 0.0f || vs.noiseGainL != 0.0f || vs.noiseGainR != 0.0f)
    {
        float tl, tr;
        panGains (engineParam (p, lp::wtNoisePan), tl, tr);
        tl *= nl; tr *= nl;
        const bool pink = engineChoice (p, lp::wtNoiseType) == 1;
        const float dl = (tl - vs.noiseGainL) / (float) n, dr = (tr - vs.noiseGainR) / (float) n;
        auto& b = vs.pink;
        for (int i = 0; i < n; ++i)
        {
            float x = white (vs.rngL);
            if (pink)
            {
                b[0] = 0.99765f * b[0] + x * 0.0990460f;
                b[1] = 0.96300f * b[1] + x * 0.2965164f;
                b[2] = 0.57000f * b[2] + x * 1.0526913f;
                x = (b[0] + b[1] + b[2] + x * 0.1848f) * 0.25f;
            }
            vs.noiseGainL += dl; vs.noiseGainR += dr;
            left[i] += x * vs.noiseGainL;
            right[i] += x * vs.noiseGainR;
        }
        vs.noiseGainL = tl; vs.noiseGainR = tr;
    }
    return false;
}
} // namespace chroma
