#include "AnalogEngine.h"
#include "engines/EngineParams.h"
#include "shared/Blep.h"
#include <algorithm>
#include <cmath>

namespace chroma
{
namespace
{
constexpr double twoPi = 6.283185307179586;
constexpr float maxInc = 0.45f;   // phase increment limit (per oversampled sample)

inline double wrap (double p) noexcept { return p - std::floor (p); }

inline void panGains (float pan, float& gl, float& gr) noexcept
{
    const float angle = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.25f * 3.14159265f;
    gl = std::cos (angle) * 1.41421356f;
    gr = std::sin (angle) * 1.41421356f;
}

inline uint32_t lcg (uint32_t& s) noexcept { s = s * 1664525u + 1013904223u; return s; }
inline double rand01 (uint32_t& s) noexcept { return (double) (lcg (s) >> 8) / 16777216.0; }

inline float noteToInc (float pitch, double rate) noexcept
{
    const double f = 440.0 * std::exp2 ((pitch - 69.0f) / 12.0f) / rate;
    return (float) std::min (f, (double) maxInc);
}

// SuperSaw side voice j (1-based) -> signed position in -1..1 (alternating sides, outermost = ±1).
inline float superPos (int j, int count) noexcept
{
    if (j == 0) return 0.0f;
    const int sides = (count - 1 + 1) / 2;
    const float mag = (float) ((j + 1) / 2) / (float) std::max (1, sides);
    return (j & 1) ? mag : -mag;
}
} // namespace

// ------------------------------------------------------------------ shapes
AnalogEngine::Shape AnalogEngine::makeShape (int wave, float m, float w) noexcept
{
    Shape s;
    s.wave = wave;
    s.m = std::clamp (m, 0.0f, 1.0f);
    s.w = std::clamp (w, 0.005f, 0.995f);
    constexpr float tp = 6.2831853f;
    switch (wave)
    {
        case Triangle:
        {
            const float q = 0.5f + 0.49f * s.m, j = 2.0f / q + 2.0f / (1.0f - q);
            s.w = q;
            s.nbp = 2; s.bp = { 0.0f, q }; s.dv = { 0.0f, 0.0f }; s.ds = { j, -j };
            break;
        }
        case Saw: case SuperSaw:
            if (wave == SuperSaw) s.m = 0.0f;
            s.nbp = s.m > 0.0f ? 2 : 1; s.bp = { 0.0f, 0.5f }; s.dv = { -2.0f, -2.0f * s.m }; s.ds = { 0.0f, 0.0f };
            break;
        case Square:
            s.nbp = 2; s.bp = { 0.0f, 0.5f }; s.dv = { 2.0f - 4.0f * s.m, -2.0f * (1.0f - s.m) }; s.ds = { 0.0f, 0.0f };
            break;
        case Pulse: case Narrow:
            s.nbp = 2; s.bp = { 0.0f, s.w }; s.dv = { 2.0f, -2.0f }; s.ds = { 0.0f, 0.0f };
            break;
        case Half:
        {
            const float j = 2.0f * tp * (1.0f + s.m);
            s.nbp = 2; s.bp = { 0.0f, 0.5f }; s.dv = { 0.0f, 0.0f }; s.ds = { j, j };
            break;
        }
        default: break;   // Sine: smooth
    }
    return s;
}

float AnalogEngine::Shape::value (double p) const noexcept
{
    switch (wave)
    {
        case Sine:
        {
            const double x = twoPi * p;
            return (float) std::sin (m > 0.0f ? x + 1.5 * m * std::sin (x) : x);
        }
        case Triangle: return p < w ? -1.0f + 2.0f * (float) p / w : 1.0f - 2.0f * ((float) p - w) / (1.0f - w);
        case Saw: case SuperSaw:
        {
            const float a = 2.0f * (float) p - 1.0f;
            if (m == 0.0f) return a;
            return a + m * (p < 0.5 ? 2.0f * (float) p : 2.0f * (float) p - 2.0f);
        }
        case Square: return (1.0f - m) * (p < 0.5 ? 1.0f : -1.0f) + m * (2.0f * (float) p - 1.0f);
        case Pulse: case Narrow: return (p < w ? 1.0f : -1.0f) - (2.0f * w - 1.0f);
        case Half:
        {
            const float sn = (float) std::sin (twoPi * p);
            return 2.0f * ((sn > 0.0f ? sn : -m * sn) - (1.0f + m) * 0.318309886f);
        }
        default: return 0.0f;
    }
}

float AnalogEngine::Shape::slope (double p) const noexcept
{
    constexpr float tp = 6.2831853f;
    switch (wave)
    {
        case Sine:
        {
            const double x = twoPi * p;
            const double k = 1.5 * m;
            return (float) (std::cos (x + k * std::sin (x)) * twoPi * (1.0 + k * std::cos (x)));
        }
        case Triangle: return p < w ? 2.0f / w : -2.0f / (1.0f - w);
        case Saw: case SuperSaw: return 2.0f + 2.0f * m;
        case Square: return 2.0f * m;
        case Half:
        {
            const float c = (float) std::cos (twoPi * p) * tp * 2.0f;
            return p < 0.5 ? c : -m * c;
        }
        default: return 0.0f;
    }
}

// ------------------------------------------------------------------ lifecycle
void AnalogEngine::prepare (double sampleRate, int maxBlockSize, int maxVoices)
{
    sr = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    maxN = maxBlock * maxOversampling;
    voices.assign ((size_t) std::max (1, maxVoices), VoiceState {});
    for (int k = 0; k < numOsc; ++k)
    {
        oscL[(size_t) k].assign ((size_t) maxN + 8, 0.0f);
        oscR[(size_t) k].assign ((size_t) maxN + 8, 0.0f);
    }
    for (auto* b : { &subBuf, &busFL, &busFR, &busBL, &busBR, &fmBuf })
        b->assign ((size_t) maxN + 8, 0.0f);
    work.assign ((size_t) Decimator::workSize (maxN) + 8, 0.0f);
    syncX.assign ((size_t) (maxUnison * maxN), -1.0f);
    (void) Halfband::longStage();   // design the filters off the audio thread
    (void) Halfband::shortStage();
    reset();
}

void AnalogEngine::reset()
{
    for (size_t v = 0; v < voices.size(); ++v)
        kill ((int) v);
}

void AnalogEngine::kill (int voice)
{
    auto& vs = voices[(size_t) voice];
    const uint32_t seed = vs.seed;
    vs = VoiceState {};
    vs.seed = seed == 1 ? 0x9E3779B9u * (uint32_t) (voice + 1) : seed;
    vs.noise.rngL = 0x1234567u + (uint32_t) voice * 7919u;
    vs.noise.rngR = 0x7654321u + (uint32_t) voice * 104729u;
}

void AnalogEngine::setOversampling (int factor)
{
    const int f = factor >= 8 ? 8 : factor >= 4 ? 4 : factor >= 2 ? 2 : 1;
    if (f == os)
        return;
    os = f;
    for (auto& vs : voices)
        for (auto& d : vs.dec) d.reset();
}

void AnalogEngine::noteOn (int voice, const EngineNote& n)
{
    if (n.retrigger)
        voices[(size_t) voice].pendingStart = true;
}

// ------------------------------------------------------------------ oscillators
void AnalogEngine::setupOsc (OscRender& r, const EngineVoiceParams& p, int k, float pitch0, float pitch1) const noexcept
{
    const auto u = (size_t) k;
    const int wave = std::clamp (engineChoice (p, lp::anOscWave[u]), 0, 7);
    const float wm = engineParam (p, lp::anOscWaveMod[u]);
    float w = engineParam (p, lp::anOscPw[u]) * 0.01f;
    if (wave == Pulse) w += (0.01f - w) * wm;
    if (wave == Narrow) w = 0.1f - 0.09f * wm;
    r.shape = makeShape (wave, wm, w);
    r.resetPhase = engineParam (p, lp::anOscPhase[u]) / 360.0f;

    r.unison = std::clamp (p.unison, 1, maxUnison);
    r.superCount = wave == SuperSaw ? std::clamp (engineChoice (p, lp::anOscSsVoices[u]), 2, maxSuper) : 1;
    r.subCount = r.unison * r.superCount;

    const float offset = 12.0f * (float) engineChoice (p, lp::anOscOctave[u]) + (float) engineChoice (p, lp::anOscSemi[u])
                       + engineParam (p, lp::anOscFine[u]) * 0.01f;
    const float width = engineParam (p, lp::anOscWidth[u]);
    const float uNorm = 1.0f / std::sqrt ((float) r.unison);
    const double rate = sr * os;

    // SuperSaw: detune curve (100% = ±1 st) and centre/side balance from Wave Mod.
    const float d = engineParam (p, lp::anOscSsDetune[u]);
    const float ssCents = 100.0f * (0.3f * d + 0.7f * d * d * d);
    const float ssSpread = engineParam (p, lp::anOscSsSpread[u]);
    const float centreG = 1.0f - 0.6f * wm, sideG = 0.6f + 0.4f * wm;
    const float ssNorm = 1.0f / std::sqrt (centreG * centreG + (float) (r.superCount - 1) * sideG * sideG);

    for (int uu = 0; uu < r.unison; ++uu)
    {
        const float posU = r.unison > 1 ? (float) uu / (float) (r.unison - 1) - 0.5f : 0.0f;
        float cents = posU * p.detuneCents;
        if (p.driftSemis != nullptr)
            cents += p.driftSemis[(size_t) ((uu + 5 * k) % maxUnison)] * 100.0f;
        const float panU = p.unisonPan + 2.0f * posU * p.spread * width;
        for (int s = 0; s < r.superCount; ++s)
        {
            const auto j = (size_t) (uu * r.superCount + s);
            float c = cents, pan = panU, g = uNorm;
            if (r.superCount > 1)
            {
                const float pos = superPos (s, r.superCount);
                c += pos * ssCents;
                pan += pos * ssSpread * width;
                g *= (s == 0 ? centreG : sideG) * ssNorm;
            }
            float gl, gr;
            panGains (pan, gl, gr);
            r.gl[j] = gl * g;
            r.gr[j] = gr * g;
            r.inc0[j] = noteToInc (pitch0 + offset + c * 0.01f, rate);
            r.inc1[j] = noteToInc (pitch1 + offset + c * 0.01f, rate);
        }
    }
}

void AnalogEngine::renderOsc (VoiceState& vs, int k, const OscRender& r, int N, const float* fm,
                              bool recordSync, bool useSync) noexcept
{
    auto& o = vs.osc[(size_t) k];
    float* L = oscL[(size_t) k].data();
    float* R = oscR[(size_t) k].data();
    std::fill (L + 4, L + N + 4, 0.0f);
    std::fill (R + 4, R + N + 4, 0.0f);
    std::copy (o.tailL.begin(), o.tailL.end(), L);
    std::copy (o.tailR.begin(), o.tailR.end(), R);

    if (recordSync)
        std::fill (syncX.begin(), syncX.begin() + r.unison * maxN, -1.0f);

    const Shape& sh = r.shape;
    const auto rp = (double) r.resetPhase;
    const float invN = 1.0f / (float) N;

    for (int j = 0; j < r.subCount; ++j)
    {
        const auto ju = (size_t) j;
        const int u = j / r.superCount;
        const bool centre = (j % r.superCount) == 0;
        const float gl = r.gl[ju], gr = r.gr[ju];
        const float i0 = r.inc0[ju], di = (r.inc1[ju] - i0) * invN;
        float* sx = syncX.data() + u * maxN;
        double p = o.phase[ju];

        const auto emit = [&] (int pos, float x, float h, float sj) noexcept
        {
            if (h != 0.0f) { blep::addStep (L, pos, x, h * gl); blep::addStep (R, pos, x, h * gr); }
            if (sj != 0.0f) { blep::addRamp (L, pos, x, sj * gl); blep::addRamp (R, pos, x, sj * gr); }
        };
        // Breakpoint crossings while the phase moves by d (segment ends xEnd samples before the sample, lasts span).
        const auto crossings = [&] (int pos, double ph, double dlt, float inc, float xEnd, float span) noexcept
        {
            const float ainc = std::fabs (inc);
            for (int b = 0; b < sh.nbp; ++b)
            {
                const double bpv = sh.bp[(size_t) b];
                if (dlt > 0.0)
                {
                    const double bb = bpv > ph ? bpv : bpv + 1.0;
                    if (ph + dlt >= bb)
                        emit (pos, xEnd + (float) (1.0 - (bb - ph) / dlt) * span, sh.dv[(size_t) b], sh.ds[(size_t) b] * ainc);
                }
                else if (dlt < 0.0)
                {
                    const double bb = bpv <= ph ? bpv : bpv - 1.0;
                    if (ph + dlt < bb)
                        emit (pos, xEnd + (float) (1.0 - (ph - bb) / -dlt) * span, -sh.dv[(size_t) b], sh.ds[(size_t) b] * ainc);
                }
            }
        };

        for (int i = 0; i < N; ++i)
        {
            float inc = i0 + di * (float) (i + 1);
            if (fm != nullptr)
                inc = std::clamp (inc * (1.0f + fm[i]), -maxInc, maxInc);
            const int pos = i + 2;
            const float x = useSync ? sx[i] : -1.0f;
            if (x >= 0.0f)
            {
                const double d1 = (double) inc * (1.0 - x);
                crossings (pos, p, d1, inc, x, 1.0f - x);
                const double pe = wrap (p + d1);
                emit (pos, x, sh.value (rp) - sh.value (pe), (sh.slope (rp) - sh.slope (pe)) * inc);
                const double d2 = (double) inc * x;
                crossings (pos, rp, d2, inc, 0.0f, x);
                p = wrap (rp + d2);
            }
            else
            {
                if (recordSync && centre && p + inc >= 1.0)
                    sx[i] = (float) ((p + inc - 1.0) / inc);
                crossings (pos, p, inc, inc, 0.0f, 1.0f);
                p += inc;
                if (p >= 1.0) p -= 1.0;
                else if (p < 0.0) p += 1.0;
            }
            const float v = sh.value (p);
            L[pos] += v * gl;
            R[pos] += v * gr;
        }
        o.phase[ju] = p;
    }

    std::copy (L + N, L + N + 4, o.tailL.begin());
    std::copy (R + N, R + N + 4, o.tailR.begin());
}

void AnalogEngine::renderSub (VoiceState& vs, int wave, float pitch0, float pitch1, int N) noexcept
{
    float* B = subBuf.data();
    std::fill (B + 4, B + N + 4, 0.0f);
    std::copy (vs.subTail.begin(), vs.subTail.end(), B);
    const Shape sh = makeShape (wave == 0 ? Sine : Square, 0.0f, 0.5f);
    const double rate = sr * os;
    const float i0 = noteToInc (pitch0, rate), di = (noteToInc (pitch1, rate) - i0) / (float) N;
    double p = vs.subPhase;
    for (int i = 0; i < N; ++i)
    {
        const float inc = i0 + di * (float) (i + 1);
        for (int b = 0; b < sh.nbp; ++b)
        {
            const double bpv = sh.bp[(size_t) b];
            const double bb = bpv > p ? bpv : bpv + 1.0;
            if (p + inc >= bb)
                blep::addStep (B, i + 2, (float) ((p + inc - bb) / inc), sh.dv[(size_t) b]);
        }
        p = wrap (p + inc);
        B[i + 2] += sh.value (p);
    }
    vs.subPhase = p;
    std::copy (B + N, B + N + 4, vs.subTail.begin());
}

void AnalogEngine::renderNoise (VoiceState& vs, const EngineVoiceParams& p, float* l, float* r, int n,
                                float gl1, float gr1) noexcept
{
    auto& ns = vs.noise;
    const bool pink = engineChoice (p, lp::anNoiseType) == 1;
    const float color = engineParam (p, lp::anNoiseColor);
    const float tilt = color < 0.0f ? color : 3.0f * color;
    const float width = engineParam (p, lp::anNoiseWidth);
    const float rNorm = 1.0f / std::sqrt (width * width + (1.0f - width) * (1.0f - width));
    const float a = 1.0f - std::exp (-6.2831853f * 1000.0f / (float) sr);
    const float dgl = (gl1 - ns.gainL) / (float) n, dgr = (gr1 - ns.gainR) / (float) n;

    const auto white = [] (uint32_t& s) noexcept { return (float) (lcg (s) >> 8) * (2.0f / 16777216.0f) - 1.0f; };
    const auto colour = [&] (float w, std::array<float, 3>& b, float& lp) noexcept
    {
        float x = w;
        if (pink)
        {
            b[0] = 0.99765f * b[0] + w * 0.0990460f;
            b[1] = 0.96300f * b[1] + w * 0.2965164f;
            b[2] = 0.57000f * b[2] + w * 1.0526913f;
            x = (b[0] + b[1] + b[2] + w * 0.1848f) * 0.25f;
        }
        lp += a * (x - lp);
        return x + tilt * (x - lp);
    };

    for (int i = 0; i < n; ++i)
    {
        const float nl = colour (white (ns.rngL), ns.pinkL, ns.lpL);
        const float nr = width > 0.0f ? colour (white (ns.rngR), ns.pinkR, ns.lpR) : 0.0f;
        ns.gainL += dgl; ns.gainR += dgr;
        l[i] += nl * ns.gainL;
        r[i] += (width > 0.0f ? (width * nr + (1.0f - width) * nl) * rNorm : nl) * ns.gainR;
    }
    ns.gainL = gl1; ns.gainR = gr1;
}

// ------------------------------------------------------------------ render
bool AnalogEngine::render (int voice, const EngineVoiceParams& p, float* left, float* right, int n) noexcept
{
    auto& vs = voices[(size_t) voice];
    bool bypass = false;
    for (int done = 0; done < n;)
    {
        const int m = std::min (maxBlock, n - done);
        const float a0 = (float) done / (float) n, a1 = (float) (done + m) / (float) n;
        const float p0 = p.pitchStart + (p.pitchEnd - p.pitchStart) * a0;
        const float p1 = p.pitchStart + (p.pitchEnd - p.pitchStart) * a1;
        float* bl = p.bypassL != nullptr ? p.bypassL + done : nullptr;
        float* br = p.bypassR != nullptr ? p.bypassR + done : nullptr;
        const bool b = renderBlock (vs, p, left + done, right + done, bl, br, m, p0, p1);
        if (b && ! bypass && done > 0)   // bypass bus starts mid-call: clear the part already rendered
        {
            std::fill (p.bypassL, p.bypassL + done, 0.0f);
            std::fill (p.bypassR, p.bypassR + done, 0.0f);
        }
        else if (! b && bypass)
        {
            std::fill (bl, bl + m, 0.0f);
            std::fill (br, br + m, 0.0f);
        }
        bypass = bypass || b;
        done += m;
    }
    return bypass;
}

bool AnalogEngine::renderBlock (VoiceState& vs, const EngineVoiceParams& p, float* left, float* right,
                                float* byL, float* byR, int n, float pitch0, float pitch1) noexcept
{
    const int N = n * os;
    const bool canBypass = byL != nullptr && byR != nullptr;
    const float master = engineParam (p, lp::anMasterLevel);

    // --- note start: phases
    if (vs.pendingStart)
    {
        vs.pendingStart = false;
        for (int k = 0; k < numOsc; ++k)
        {
            const auto u = (size_t) k;
            const bool reset = engineSwitch (p, lp::anOscReset[u]);
            const size_t sc = engineChoice (p, lp::anOscWave[u]) == SuperSaw
                                ? (size_t) std::clamp (engineChoice (p, lp::anOscSsVoices[u]), 2, maxSuper) : 1;
            const double ph = engineParam (p, lp::anOscPhase[u]) / 360.0;
            auto& o = vs.osc[u];
            for (size_t j = 0; j < o.phase.size(); ++j)   // SuperSaw side voices always free-running
                o.phase[j] = (reset && j % sc == 0) ? wrap (ph) : rand01 (vs.seed);
        }
        vs.subPhase = wrap (engineParam (p, lp::anSubPhase) / 360.0);
    }

    // --- which sources run
    bool anySolo = false;
    std::array<bool, numOsc> on {}, audible {};
    for (int k = 0; k < numOsc; ++k)
    {
        on[(size_t) k] = engineSwitch (p, lp::anOscOn[(size_t) k]);
        anySolo = anySolo || (on[(size_t) k] && engineSwitch (p, lp::anMixSolo[(size_t) k]));
    }
    for (int k = 0; k < numOsc; ++k)
    {
        const auto u = (size_t) k;
        audible[u] = on[u] && ! engineSwitch (p, lp::anMixMute[u]) && (! anySolo || engineSwitch (p, lp::anMixSolo[u]));
    }
    const bool sync = engineSwitch (p, lp::anSync);
    const float fm3 = engineParam (p, lp::anFm3to1), fm2 = engineParam (p, lp::anFm2to1);
    const float ring = engineParam (p, lp::anRing);
    const bool osc1Runs = on[0] && (audible[0] || vs.osc[0].gainL + vs.osc[0].gainR > 0.0f);
    const bool use3 = osc1Runs && on[2] && fm3 > 0.0f;
    const bool use2 = osc1Runs && on[1] && (sync || fm2 > 0.0f || ring > 0.0f);

    std::array<float, numOsc> tgL {}, tgR {};
    std::array<bool, numOsc> run {};
    for (int k = 0; k < numOsc; ++k)
    {
        const auto u = (size_t) k;
        if (audible[u])
        {
            const float lvl = engineParam (p, lp::anOscLevel[u]) * engineParam (p, lp::anMixVolume[u]) * master;
            panGains (engineParam (p, lp::anOscPan[u]) + engineParam (p, lp::anMixPan[u]), tgL[u], tgR[u]);
            tgL[u] *= lvl; tgR[u] *= lvl;
        }
        auto& o = vs.osc[u];
        run[u] = (on[u] && (audible[u] || (k == 1 && use2) || (k == 2 && use3))) || o.gainL > 0.0f || o.gainR > 0.0f;
        if (run[u] && ! o.active) { o.tailL.fill (0.0f); o.tailR.fill (0.0f); }
        o.active = run[u];
        if (! run[u]) { o.gainL = o.gainR = 0.0f; }
    }

    // --- render oscillators (modulators first)
    OscRender r;
    const auto monoInto = [&] (int k, float scale, bool add) noexcept
    {
        const float* L = oscL[(size_t) k].data();
        const float* R = oscR[(size_t) k].data();
        for (int i = 0; i < N; ++i)
        {
            const float v = 0.5f * (L[i] + R[i]) * scale;
            fmBuf[(size_t) i] = add ? fmBuf[(size_t) i] + v : v;
        }
    };
    if (run[2]) { setupOsc (r, p, 2, pitch0, pitch1); renderOsc (vs, 2, r, N, nullptr, false, false); }
    if (run[1]) { setupOsc (r, p, 1, pitch0, pitch1); renderOsc (vs, 1, r, N, nullptr, use2 && sync, false); }
    if (run[0])
    {
        const bool fmOn = (use3 && run[2]) || (use2 && run[1] && fm2 > 0.0f);
        if (use3 && run[2]) monoInto (2, 4.0f * fm3, false);
        if (use2 && run[1] && fm2 > 0.0f) monoInto (1, 4.0f * fm2, use3 && run[2]);
        setupOsc (r, p, 0, pitch0, pitch1);
        renderOsc (vs, 0, r, N, fmOn ? fmBuf.data() : nullptr, false, use2 && sync && run[1]);

        // Ring 1×2: crossfade OSC 1 towards OSC 1 × OSC 2 (after the tails were saved)
        const float ringTarget = use2 && run[1] ? ring : 0.0f;
        if (ringTarget > 0.0f || vs.ring > 0.0f)
        {
            float* L = oscL[0].data();
            float* R = oscR[0].data();
            const float* L2 = oscL[1].data();
            const float* R2 = oscR[1].data();
            const float dr = (ringTarget - vs.ring) / (float) N;
            float rg = vs.ring;
            for (int i = 0; i < N; ++i)
            {
                rg += dr;
                const float g = 1.0f - rg + rg * 0.5f * (L2[i] + R2[i]);
                L[i] *= g; R[i] *= g;
            }
        }
        vs.ring = ringTarget;
    }

    // --- sub (follows OSC 1 pitch)
    const bool subOn = engineSwitch (p, lp::anSubOn) && (! anySolo);
    const float subTarget = subOn ? engineParam (p, lp::anSubLevel) * master : 0.0f;
    const bool subRun = subTarget > 0.0f || vs.subGain > 0.0f;
    if (subRun)
    {
        if (! vs.subActive) vs.subTail.fill (0.0f);
        const float off = 12.0f * (float) engineChoice (p, lp::anOscOctave[0]) + (float) engineChoice (p, lp::anOscSemi[0])
                        + engineParam (p, lp::anOscFine[0]) * 0.01f - (engineChoice (p, lp::anSubOctave) == 1 ? 24.0f : 12.0f)
                        + (p.driftSemis != nullptr ? p.driftSemis[0] : 0.0f);
        renderSub (vs, engineChoice (p, lp::anSubWave), pitch0 + off, pitch1 + off, N);
    }
    vs.subActive = subRun;

    // --- mixer -> filter / bypass buses (oversampled)
    std::array<bool, 2> busUsed {};
    std::array<float*, 2> busL { busFL.data(), busBL.data() }, busR { busFR.data(), busBR.data() };
    std::array<bool, 2> busClear { true, true };
    const auto mix = [&] (int route, const float* sl, const float* sr_, float& gl, float& gr, float tl, float tr) noexcept
    {
        const int b = canBypass ? route : 0;
        float* dl = busL[(size_t) b];
        float* dr = busR[(size_t) b];
        if (busClear[(size_t) b]) { std::fill (dl, dl + N, 0.0f); std::fill (dr, dr + N, 0.0f); busClear[(size_t) b] = false; }
        busUsed[(size_t) b] = true;
        const float sL = (tl - gl) / (float) N, sR = (tr - gr) / (float) N;
        for (int i = 0; i < N; ++i)
        {
            gl += sL; gr += sR;
            dl[i] += sl[i] * gl;
            dr[i] += sr_[i] * gr;
        }
        gl = tl; gr = tr;
    };
    for (int k = 0; k < numOsc; ++k)
    {
        const auto u = (size_t) k;
        auto& o = vs.osc[u];
        if (run[u] && (audible[u] || o.gainL > 0.0f || o.gainR > 0.0f))
            mix (engineChoice (p, lp::anMixRoute[u]), oscL[u].data(), oscR[u].data(), o.gainL, o.gainR, tgL[u], tgR[u]);
    }
    if (subRun)
    {
        float g = vs.subGain, g2 = vs.subGain;
        mix (engineChoice (p, lp::anSubRoute), subBuf.data(), subBuf.data(), g, g2, subTarget, subTarget);
        vs.subGain = subTarget;
    }

    // --- decimate
    const auto decimate = [&] (int b, float* outL, float* outR) noexcept
    {
        auto& hold = vs.busHold[(size_t) b];
        if (busUsed[(size_t) b]) hold = 256;
        else if (hold > 0) hold = std::max (0, hold - n);
        else return false;
        if (busClear[(size_t) b]) { std::fill (busL[(size_t) b], busL[(size_t) b] + N, 0.0f); std::fill (busR[(size_t) b], busR[(size_t) b] + N, 0.0f); }
        vs.dec[(size_t) (2 * b)].process (busL[(size_t) b], outL, n, os, work.data());
        vs.dec[(size_t) (2 * b + 1)].process (busR[(size_t) b], outR, n, os, work.data());
        return true;
    };
    if (! decimate (0, left, right))
    {
        std::fill (left, left + n, 0.0f);
        std::fill (right, right + n, 0.0f);
    }
    bool bypass = canBypass && decimate (1, byL, byR);

    // --- noise (base rate, after decimation)
    const float noiseLvl = (anySolo ? 0.0f : engineParam (p, lp::anNoiseLevel)) * master;
    if (noiseLvl > 0.0f || vs.noise.gainL > 0.0f || vs.noise.gainR > 0.0f)
    {
        float gl, gr;
        panGains (engineParam (p, lp::anNoisePan), gl, gr);
        const bool toBypass = canBypass && engineChoice (p, lp::anNoiseRoute) == 1;
        if (toBypass && ! bypass)
        {
            std::fill (byL, byL + n, 0.0f);
            std::fill (byR, byR + n, 0.0f);
            bypass = true;
        }
        renderNoise (vs, p, toBypass ? byL : left, toBypass ? byR : right, n, gl * noiseLvl, gr * noiseLvl);
    }
    return bypass;
}
} // namespace chroma
