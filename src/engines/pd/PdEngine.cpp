#include "PdEngine.h"
#include "engines/EngineParams.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace chroma
{
namespace
{
constexpr int tableSize = 4096;
constexpr float maxInc = 0.49f;
constexpr float pi = 3.14159265f;

struct CosTable
{
    std::array<float, tableSize + 1> v {};
    CosTable()
    {
        for (int i = 0; i <= tableSize; ++i)
            v[(size_t) i] = (float) std::cos (6.283185307179586 * (double) i / (double) tableSize);
    }
};
const CosTable& cosTable() { static const CosTable t; return t; }

// cos(2π x), any x
inline float cos01 (float x) noexcept
{
    const auto& t = cosTable().v;
    const float f = (x - std::floor (x)) * (float) tableSize;
    const int i = std::min ((int) f, tableSize - 1);
    return t[(size_t) i] + (t[(size_t) i + 1] - t[(size_t) i]) * (f - (float) i);
}

inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

// Two-segment linear bend of [0,1] through (k, 0.5).
inline float bend (float p, float k) noexcept
{
    return p < k ? 0.5f * p / k : 0.5f + 0.5f * (p - k) / (1.0f - k);
}

// Rational curve of [0,1] with bounded slopes (1/k at 0, k at 1): k > 1 slow start, k < 1 fast start.
inline float rcurve (float x, float k) noexcept
{
    const float d = x + k * (1.0f - x);
    return d > 0.0f ? x / d : 0.0f;
}

// Segment easing of the shape families (u in [0,1], ends fixed).
inline float ease (int shape, float u) noexcept
{
    switch (shape)
    {
        case PdEngine::Smooth: return 0.5f - 0.5f * cos01 (0.5f * u);
        case PdEngine::Ripple: return u - 0.1f * cos01 (2.0f * u + 0.25f);          // u + 0.1·sin(4πu)
        case PdEngine::Steps:  return u + cos01 (4.0f * u + 0.25f) * (1.0f / (8.0f * pi)); // u − sin(8πu)/8π
        default:               return u;
    }
}

inline void panGains (float pan, float& gl, float& gr) noexcept
{
    const float angle = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.25f * pi;
    gl = std::cos (angle) * 1.41421356f;
    gr = std::sin (angle) * 1.41421356f;
}

inline float noteHz (float pitch) noexcept { return 440.0f * std::exp2 ((pitch - 69.0f) / 12.0f); }

static_assert (sizeof (PdEngine::Ctl) == PdEngine::numCtl * sizeof (float));
inline void toArray (const PdEngine::Ctl& c, float* a) noexcept { std::memcpy (a, static_cast<const void*> (&c), sizeof (PdEngine::Ctl)); }
inline void fromArray (const float* a, PdEngine::Ctl& c) noexcept { std::memcpy (static_cast<void*> (&c), a, sizeof (PdEngine::Ctl)); }

EnvParams pdEnv (const EngineVoiceParams& p, size_t l) noexcept
{
    EnvParams e;
    e.attack = engineParam (p, lp::pdEnvAttack[l]);
    e.decay = engineParam (p, lp::pdEnvDecay[l]);
    e.sustain = engineParam (p, lp::pdEnvSustain[l]);
    e.release = engineParam (p, lp::pdEnvRelease[l]);
    e.velocity = engineParam (p, lp::pdEnvVelocity[l]);
    e.keyTrack = engineParam (p, lp::pdEnvKeytrack[l]);
    return e;
}
} // namespace

// ------------------------------------------------------------------ phase map
void PdEngine::prepareMap (Map& m, int wave, int shape, const Ctl& c, const pd::PhaseCurve* curve) noexcept
{
    const float a = std::clamp (c.amount, 0.0f, 1.0f);
    const float s = std::clamp (c.symmetry, 0.0f, 1.0f);
    const float lo = std::clamp (c.kneeMin, 0.001f, 0.45f), hi = 1.0f - lo;
    m.wave = std::clamp (wave, 0, (int) Reso3);
    m.shapeA = std::clamp (shape, 0, numShapes - 1);
    m.shapeB = (m.shapeA + 1) % numShapes;
    m.amount = a;
    m.morph = std::clamp (c.morph, 0.0f, 1.0f);
    m.curveExp = c.curveExp;
    m.asymKnee = 0.5f - 0.45f * std::clamp (c.asymmetry, -1.0f, 1.0f);
    m.warpExp = c.warpExp;
    m.offset = 0.5f * std::clamp (c.offset, -1.0f, 1.0f);
    m.window = std::clamp (c.window, 0.0f, 1.0f);
    m.mix = std::clamp (c.mix, 0.0f, 1.0f);
    m.reso = std::clamp (1.0f + 15.0f * a, 1.0f, std::max (1.0f, c.resoMax));
    m.windowKnee = lerp (0.5f, 0.05f, s);
    m.custom = curve;

    auto& x = m.kx;
    auto& y = m.ky;
    switch (m.wave)
    {
        case Saw:
        {
            const float k = lerp (0.5f, lerp (lo, hi, s), a);
            x = { 0.0f, k, 1.0f }; y = { 0.0f, 0.5f, 1.0f }; m.numKnots = 3;
            break;
        }
        case Square:
        {
            const float r = lerp (1.0f, lerp (std::min (1.0f, 2.0f * lo), 1.0f, s), a);
            x = { 0.0f, 0.5f * r, 0.5f, 0.5f + 0.5f * r, 1.0f }; y = { 0.0f, 0.5f, 0.5f, 1.0f, 1.0f }; m.numKnots = 5;
            break;
        }
        case Pulse:
        {
            const float k = lerp (1.0f, lerp (lo, hi, s), a);
            x = { 0.0f, k, 1.0f }; y = { 0.0f, 1.0f, 1.0f }; m.numKnots = 3;
            break;
        }
        case DoubleSine:
        {
            const float lo2 = std::min (0.45f, 2.0f * lo);
            const float k = 0.5f * lerp (0.5f, lerp (lo2, 1.0f - lo2, s), a);
            x = { 0.0f, k, 0.5f, 0.5f + k, 1.0f }; y = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }; m.numKnots = 5;
            break;
        }
        case SawPulse:
        {
            const float p = 0.5f + 0.45f * s;
            x = { 0.0f, lerp (0.5f * p, std::min (lo, 0.5f * p), a), p, 1.0f };
            y = { 0.0f, lerp (0.5f * p, 0.5f, a), lerp (p, 1.0f, a), 1.0f };
            m.numKnots = 4;
            break;
        }
        default:   // Reso: identity map, the resonance runs k times faster under the window
            x = { 0.0f, 1.0f }; y = { 0.0f, 1.0f }; m.numKnots = 2;
            break;
    }
}

float PdEngine::mapPhase (const Map& m, float ph) noexcept
{
    float p = ph;
    if (m.asymKnee != 0.5f)
        p = bend (p, m.asymKnee);
    if (m.warpExp != 1.0f)
        p = rcurve (p, m.warpExp);
    const bool reso = m.wave >= Reso1;
    const float depth = reso ? 1.0f : m.amount;   // non-Reso: Amount 0 = pure sine whatever the shape

    // segment of the knot map
    int k = 0;
    while (k + 2 < m.numKnots && p >= m.kx[(size_t) k + 1])
        ++k;
    const float x0 = m.kx[(size_t) k], x1 = m.kx[(size_t) k + 1], y0 = m.ky[(size_t) k], y1 = m.ky[(size_t) k + 1];
    const float w = x1 - x0;
    float u = w > 1.0e-7f ? std::clamp ((p - x0) / w, 0.0f, 1.0f) : 1.0f;
    const float uc = m.curveExp != 1.0f ? rcurve (u, m.curveExp) : u;
    const float pc = m.curveExp != 1.0f ? rcurve (p, m.curveExp) : p;

    const auto one = [&] (int shape)
    {
        if (shape == Custom)
        {
            const float cy = m.custom != nullptr ? m.custom->evaluate (pc) : pc;
            // custom curve replaces the knot map (it closes the cycle at (1,1))
            return reso ? cy : p + (cy - p) * depth;
        }
        const float ue = u + (ease (shape, uc) - u) * depth;
        return y0 + (y1 - y0) * ue;
    };
    if (m.morph <= 0.0f) return one (m.shapeA);
    if (m.morph >= 1.0f) return one (m.shapeB);
    return lerp (one (m.shapeA), one (m.shapeB), m.morph);
}

float PdEngine::evaluate (const Map& m, float ph) noexcept
{
    const float t = mapPhase (m, ph);
    float y;
    if (m.wave >= Reso1)
    {
        const float pw = bend (ph, m.windowKnee);
        const float win = m.wave == Reso1 ? 1.0f - pw
                        : m.wave == Reso2 ? 1.0f - std::abs (2.0f * pw - 1.0f)
                        : (pw < 0.5f ? 1.0f : 2.0f * (1.0f - pw));
        y = 1.0f - win * (1.0f - cos01 (m.reso * t));
    }
    else
        y = cos01 (t + m.offset);
    if (m.mix < 1.0f)
    {
        const float sine = cos01 (ph);
        y = sine + (y - sine) * m.mix;
    }
    if (m.window > 0.0f)
        y *= 1.0f - m.window * (0.5f + 0.5f * cos01 (ph));
    return y;
}

float PdEngine::distortedPhase (int wave, int shape, const Ctl& c, float ph, const pd::PhaseCurve* curve) noexcept
{
    Map m;
    prepareMap (m, wave, shape, c, curve);
    return mapPhase (m, ph);
}

float PdEngine::lineValue (int wave, int shape, const Ctl& c, float ph, const pd::PhaseCurve* curve) noexcept
{
    Map m;
    prepareMap (m, wave, shape, c, curve);
    return evaluate (m, ph);
}

// ------------------------------------------------------------------ lifecycle
void PdEngine::prepare (double sampleRate, int maxBlockSize, int maxVoices)
{
    sr = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    const int maxN = maxBlock * maxOversampling;
    voices.assign ((size_t) std::max (1, maxVoices), VoiceState {});
    busL.assign ((size_t) maxN + 8, 0.0f);
    busR.assign ((size_t) maxN + 8, 0.0f);
    work.assign ((size_t) Decimator::workSize (maxN) + 8, 0.0f);
    (void) cosTable();
    (void) Halfband::longStage();
    (void) Halfband::shortStage();
    reset();
}

void PdEngine::reset()
{
    for (size_t v = 0; v < voices.size(); ++v)
        kill ((int) v);
}

void PdEngine::kill (int voice)
{
    auto& vs = voices[(size_t) voice];
    const uint32_t seed = 0x9e3779b9u * (uint32_t) (voice + 1);
    vs = VoiceState {};
    vs.rng = seed;
    for (auto& l : vs.line) l.env.setSampleRate (sr);
}

void PdEngine::setOversampling (int factor)
{
    const int f = factor >= 8 ? 8 : factor >= 4 ? 4 : factor >= 2 ? 2 : 1;
    if (f == os)
        return;
    os = f;
    for (auto& vs : voices)
    {
        for (auto& d : vs.dec) d.reset();
        vs.first = true;
    }
}

void PdEngine::noteOn (int voice, const EngineNote& n)
{
    auto& vs = voices[(size_t) voice];
    vs.note = n.note;
    if (n.retrigger || ! vs.started)
    {
        vs.velocity = n.velocity;
        vs.pendingStart = true;
        vs.pendingRelease = false;
        vs.legato = ! n.retrigger;
    }
}

void PdEngine::noteOff (int voice)
{
    voices[(size_t) voice].pendingRelease = true;
}

void PdEngine::start (VoiceState& vs, const EngineVoiceParams& p, bool retrigger) noexcept
{
    for (size_t l = 0; l < (size_t) numLines; ++l)
    {
        auto& ls = vs.line[l];
        ls.env.setSampleRate (sr);
        ls.env.noteOn (pdEnv (p, l), vs.velocity, vs.note);
        if (retrigger)
            ls.phase.fill (0.0);
    }
    if (! vs.started)
        vs.first = true;
}

// ------------------------------------------------------------------ control rate
void PdEngine::computeTargets (VoiceState& vs, const EngineVoiceParams& p, int n, float pitch,
                               std::array<LineTargets, numLines>& t, float& ring, float& noise, float& noiseCoef,
                               float& noiseGain) noexcept
{
    const double rate = sr * os;
    const int U = std::clamp (p.unison, 1, maxUnison);
    vs.unison = U;
    const float uNorm = 1.0f / std::sqrt ((float) U);
    const float out = engineParam (p, lp::pdOutput);
    ring = engineParam (p, lp::pdRing);
    noise = engineParam (p, lp::pdNoise);
    noiseCoef = 0.0f;
    noiseGain = 0.0f;

    const bool on2 = engineSwitch (p, lp::pdLineOn[1]) && engineParam (p, lp::pdLineLevel[1]) > 0.0f;
    for (size_t l = 0; l < (size_t) numLines; ++l)
    {
        auto& lt = t[l];
        auto& ls = vs.line[l];
        const EnvParams ep = pdEnv (p, l);
        ls.env.advance (ep, n);
        const float level = engineParam (p, lp::pdLineLevel[l]);
        const bool on = engineSwitch (p, lp::pdLineOn[l]);
        lt.on = on && (level > 0.0f || (l == 0 && ring > 0.0f && on2));
        lt.wave = std::clamp (engineChoice (p, lp::pdLineWave[l]), 0, (int) Reso3);
        lt.shape = std::clamp (engineChoice (p, lp::pdShape[l]), 0, numShapes - 1);

        const float semis = pitch + 12.0f * (float) engineChoice (p, lp::pdLineOctave[l])
                          + (float) engineChoice (p, lp::pdLineCoarse[l]) + 0.01f * engineParam (p, lp::pdLineFine[l])
                          + engineParam (p, lp::pdLinePitch[l]);
        const float f0 = noteHz (semis);

        auto& c = lt.ctl;
        c.amount = std::clamp (engineParam (p, lp::pdAmount[l]) + engineParam (p, lp::pdEnvAmount[l]) * ls.env.output (ep),
                               0.0f, 1.0f);
        c.morph = engineParam (p, lp::pdMorph[l]);
        c.curveExp = std::exp2 (3.0f * engineParam (p, lp::pdCurve[l]));
        c.symmetry = engineParam (p, lp::pdSymmetry[l]);
        c.asymmetry = engineParam (p, lp::pdAsymmetry[l]);
        c.offset = engineParam (p, lp::pdOffset[l]);
        c.warpExp = std::exp2 (2.0f * engineParam (p, lp::pdWarp[l]));
        c.window = engineParam (p, lp::pdWindow[l]);
        c.mix = engineParam (p, lp::pdMix[l]);
        // Band-limited knee: the fastest segment spans enough oversampled samples for the note.
        c.kneeMin = std::clamp ((float) (4.0 * f0 / rate), 0.01f, 0.45f);
        c.resoMax = std::max (1.0f, (float) (0.45 * sr / f0));
        c.phase = engineParam (p, lp::pdLinePhase[l]) / 360.0f;

        if (l == 1 && lt.on && noise > 0.0f)
        {
            const float g = 1.0f - std::exp (-2.0f * pi * std::min (f0, (float) (0.4 * rate)) / (float) rate);
            noiseCoef = g;
            noiseGain = std::min (40.0f, std::sqrt (3.0f * (2.0f - g) / g));
        }

        const float pan = engineParam (p, lp::pdLinePan[l]);
        const float g = level * out * uNorm;
        for (int u = 0; u < U; ++u)
        {
            const auto uu = (size_t) u;
            const float posU = U > 1 ? (float) u / (float) (U - 1) - 0.5f : 0.0f;
            float cents = posU * p.detuneCents;
            if (p.driftSemis != nullptr)
                cents += p.driftSemis[uu] * 100.0f;
            lt.inc[uu] = (float) std::min ((double) maxInc, noteHz (semis + 0.01f * cents) / rate);
            float gl, gr;
            panGains (pan + p.unisonPan + 2.0f * posU * p.spread, gl, gr);
            lt.gl[uu] = g * gl;
            lt.gr[uu] = g * gr;
        }
    }
}

// ------------------------------------------------------------------ audio
bool PdEngine::render (int voice, const EngineVoiceParams& p, float* left, float* right, int n) noexcept
{
    auto& vs = voices[(size_t) voice];
    for (int done = 0; done < n;)
    {
        const int m = std::min (maxBlock, n - done);
        const float a1 = (float) (done + m) / (float) n;
        renderBlock (vs, p, left + done, right + done, m, p.pitchStart + (p.pitchEnd - p.pitchStart) * a1);
        done += m;
    }
    return false;
}

void PdEngine::renderBlock (VoiceState& vs, const EngineVoiceParams& p, float* left, float* right, int n,
                            float pitch) noexcept
{
    if (vs.pendingStart)
    {
        start (vs, p, ! vs.legato || ! vs.started);
        vs.started = true;
        vs.pendingStart = false;
        vs.legato = false;
    }
    if (vs.pendingRelease)
    {
        for (size_t l = 0; l < (size_t) numLines; ++l)
            vs.line[l].env.noteOff (pdEnv (p, l));
        vs.pendingRelease = false;
    }

    std::array<LineTargets, numLines> t;
    float ring = 0.0f, noise = 0.0f, noiseCoef = 0.0f, noiseGain = 0.0f;
    computeTargets (vs, p, n, pitch, t, ring, noise, noiseCoef, noiseGain);
    if (vs.first)
    {
        for (size_t l = 0; l < (size_t) numLines; ++l)
            vs.line[l].cur = t[l];
        vs.ring = ring;
        vs.noise = noise;
        vs.first = false;
    }

    const int U = vs.unison;
    const int N = n * os;
    const float invN = 1.0f / (float) N;
    const bool any = t[0].on || t[1].on || vs.line[0].active || vs.line[1].active;
    if (! any)
    {
        std::fill (left, left + n, 0.0f);
        std::fill (right, right + n, 0.0f);
        for (size_t l = 0; l < (size_t) numLines; ++l)
            vs.line[l].cur = t[l];
        vs.dec[0].reset();
        vs.dec[1].reset();
        vs.dcxL = vs.dcyL = vs.dcxR = vs.dcyR = 0.0f;
        return;
    }

    // Per-line ramps (a line switching off fades its gains to 0 over the block)
    struct Ramp
    {
        bool run = false;
        float c[numCtl], dc[numCtl];
        std::array<float, maxUnison> inc {}, dinc {}, gl {}, dgl {}, gr {}, dgr {};
        int wave = Saw, shape = Linear;
    };
    std::array<Ramp, numLines> rp;
    for (size_t l = 0; l < (size_t) numLines; ++l)
    {
        auto& r = rp[l];
        auto& cur = vs.line[l].cur;
        const bool was = vs.line[l].active, now = t[l].on;
        r.run = was || now;
        vs.line[l].active = now;
        if (! r.run)
        {
            cur = t[l];
            continue;
        }
        if (! was)   // starting: begin at the targets with zero gain
        {
            cur.ctl = t[l].ctl;
            cur.inc = t[l].inc;
            cur.gl.fill (0.0f);
            cur.gr.fill (0.0f);
        }
        LineTargets dst = t[l];
        if (! now)
        {
            dst.ctl = cur.ctl;
            dst.inc = cur.inc;
            dst.gl.fill (0.0f);
            dst.gr.fill (0.0f);
        }
        float a[numCtl], b[numCtl];
        toArray (cur.ctl, a);
        toArray (dst.ctl, b);
        // phase offset: shortest way round so a wrap does not sweep a whole cycle
        float dph = b[numCtl - 1] - a[numCtl - 1];
        dph -= std::round (dph);
        b[numCtl - 1] = a[numCtl - 1] + dph;
        for (int i = 0; i < numCtl; ++i) { r.c[i] = a[i]; r.dc[i] = (b[i] - a[i]) * invN; }
        for (int u = 0; u < U; ++u)
        {
            const auto uu = (size_t) u;
            r.inc[uu] = cur.inc[uu]; r.dinc[uu] = (dst.inc[uu] - cur.inc[uu]) * invN;
            r.gl[uu] = cur.gl[uu];   r.dgl[uu] = (dst.gl[uu] - cur.gl[uu]) * invN;
            r.gr[uu] = cur.gr[uu];   r.dgr[uu] = (dst.gr[uu] - cur.gr[uu]) * invN;
        }
        r.wave = now ? t[l].wave : cur.wave;
        r.shape = now ? t[l].shape : cur.shape;
        dst.ctl.phase = b[numCtl - 1] - std::floor (b[numCtl - 1]);
        dst.wave = r.wave;
        dst.shape = r.shape;
        dst.on = now;
        cur = dst;
    }

    const bool ringOn = rp[0].run && rp[1].run && (ring > 0.0f || vs.ring > 0.0f);
    const bool noiseOn = rp[1].run && (noise > 0.0f || vs.noise > 0.0f);
    float rg = vs.ring, drg = (ring - vs.ring) * invN;
    float nz = vs.noise, dnz = (noise - vs.noise) * invN;
    if (noiseCoef <= 0.0f) { noiseCoef = vs.noiseCoef; noiseGain = vs.noiseGain; }
    vs.ring = ring;
    vs.noise = noise;
    vs.noiseCoef = noiseCoef;
    vs.noiseGain = noiseGain;

    std::array<float, maxUnison> y1 {};
    Map map;
    for (int s = 0; s < N; ++s)
    {
        float sl = 0.0f, sr_ = 0.0f;
        float nzv = 1.0f;
        if (noiseOn)
        {
            vs.rng = vs.rng * 1664525u + 1013904223u;
            const float w = (float) (int32_t) vs.rng * (1.0f / 2147483648.0f);
            vs.noiseLp += noiseCoef * (w - vs.noiseLp);
            nzv = 1.0f + (vs.noiseLp * noiseGain - 1.0f) * std::clamp (nz, 0.0f, 1.0f);
            nz += dnz;
        }
        for (size_t l = 0; l < (size_t) numLines; ++l)
        {
            auto& r = rp[l];
            if (! r.run)
                continue;
            Ctl c;
            fromArray (r.c, c);
            prepareMap (map, r.wave, r.shape, c, custom);
            auto& ph = vs.line[l].phase;
            for (int u = 0; u < U; ++u)
            {
                const auto uu = (size_t) u;
                float ps = (float) ph[uu] + c.phase;
                ps -= std::floor (ps);
                float y = evaluate (map, ps);
                if (l == 0)
                    y1[uu] = y;
                else
                {
                    if (ringOn)
                        y *= 1.0f + (y1[uu] - 1.0f) * std::clamp (rg, 0.0f, 1.0f);
                    y *= nzv;
                }
                sl += y * r.gl[uu];
                sr_ += y * r.gr[uu];
                ph[uu] += (double) r.inc[uu];
                if (ph[uu] >= 1.0) ph[uu] -= 1.0;
                r.inc[uu] += r.dinc[uu];
                r.gl[uu] += r.dgl[uu];
                r.gr[uu] += r.dgr[uu];
            }
            for (int i = 0; i < numCtl; ++i)
                r.c[i] += r.dc[i];
        }
        rg += drg;
        busL[(size_t) s] = sl;
        busR[(size_t) s] = sr_;
    }

    vs.dec[0].process (busL.data(), left, n, os, work.data());
    vs.dec[1].process (busR.data(), right, n, os, work.data());

    // DC blocker (Reso waves and asymmetric maps carry DC)
    const float R = 1.0f - (float) (2.0 * 3.141592653589793 * 8.0 / sr);
    for (int i = 0; i < n; ++i)
    {
        const float xl = left[i], xr = right[i];
        vs.dcyL = xl - vs.dcxL + R * vs.dcyL; vs.dcxL = xl; left[i] = vs.dcyL;
        vs.dcyR = xr - vs.dcxR + R * vs.dcyR; vs.dcxR = xr; right[i] = vs.dcyR;
    }
    if (! std::isfinite (vs.dcyL) || ! std::isfinite (vs.dcyR))
        vs.dcxL = vs.dcyL = vs.dcxR = vs.dcyR = 0.0f;
}
} // namespace chroma
