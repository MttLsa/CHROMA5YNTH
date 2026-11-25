#include "FmEngine.h"
#include "engines/EngineParams.h"
#include <algorithm>
#include <cmath>

namespace chroma
{
namespace
{
constexpr int tableSize = 4096;
constexpr float maxInc = 0.49f;

struct SineTable
{
    std::array<float, tableSize + 1> v {};
    SineTable()
    {
        for (int i = 0; i <= tableSize; ++i)
            v[(size_t) i] = (float) std::sin (6.283185307179586 * (double) i / (double) tableSize);
    }
};
const SineTable& sineTable() { static const SineTable t; return t; }

// sin(2π f) for f in [0, 1)
inline float sin01 (float f) noexcept
{
    const auto& t = sineTable().v;
    const float x = f * (float) tableSize;
    const int i = std::min ((int) x, tableSize - 1);
    const float a = x - (float) i;
    return t[(size_t) i] + (t[(size_t) i + 1] - t[(size_t) i]) * a;
}

inline float frac (double x) noexcept { return (float) (x - std::floor (x)); }

inline void panGains (float pan, float& gl, float& gr) noexcept
{
    const float angle = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.25f * 3.14159265f;
    gl = std::cos (angle) * 1.41421356f;
    gr = std::sin (angle) * 1.41421356f;
}

inline float noteHz (float pitch) noexcept { return 440.0f * std::exp2 ((pitch - 69.0f) / 12.0f); }

// Classic 6-operator algorithms: links "sd" = op s modulates op d (1-based), carriers, feedback op and
// the source of its feedback loop (itself, except algorithms 4 and 6).
struct AlgDef { const char* links; const char* carriers; int fbOp, fbSrc; };
constexpr AlgDef algDefs[FmEngine::numAlgorithms] {
    { "65 54 43 21", "13", 6, 6 },       { "65 54 43 21", "13", 2, 2 },       { "65 54 32 21", "14", 6, 6 },
    { "65 54 32 21", "14", 6, 4 },       { "65 43 21", "135", 6, 6 },         { "65 43 21", "135", 6, 5 },
    { "65 53 43 21", "13", 6, 6 },       { "65 53 43 21", "13", 4, 4 },       { "65 53 43 21", "13", 2, 2 },
    { "32 21 54 64", "14", 3, 3 },       { "32 21 54 64", "14", 6, 6 },       { "21 43 53 63", "13", 2, 2 },
    { "21 43 53 63", "13", 6, 6 },       { "21 43 54 64", "13", 6, 6 },       { "21 43 54 64", "13", 2, 2 },
    { "21 43 31 65 51", "1", 6, 6 },     { "21 43 31 65 51", "1", 2, 2 },     { "21 31 65 54 41", "1", 3, 3 },
    { "32 21 64 65", "145", 6, 6 },      { "31 32 54 64", "124", 3, 3 },      { "31 32 64 65", "1245", 3, 3 },
    { "21 63 64 65", "1345", 6, 6 },     { "32 64 65", "1245", 6, 6 },        { "63 64 65", "12345", 6, 6 },
    { "64 65", "12345", 6, 6 },          { "32 54 64", "124", 6, 6 },         { "32 54 64", "124", 3, 3 },
    { "21 54 43", "136", 5, 5 },         { "43 65", "1235", 6, 6 },           { "54 43", "1236", 5, 5 },
    { "65", "12345", 6, 6 },             { "", "123456", 6, 6 },
};

struct AlgTable
{
    std::array<FmEngine::Algorithm, FmEngine::numAlgorithms> a {};
    AlgTable()
    {
        for (int k = 0; k < FmEngine::numAlgorithms; ++k)
        {
            auto& alg = a[(size_t) k];
            const auto& d = algDefs[k];
            for (const char* s = d.links; *s != 0; ++s)
                if (s[0] >= '1' && s[0] <= '6' && s[1] >= '1' && s[1] <= '6')
                {
                    alg.conn[(size_t) (s[1] - '1')][(size_t) (s[0] - '1')] = 1.0f;
                    ++s;
                }
            for (const char* s = d.carriers; *s != 0; ++s)
                alg.carrier[(size_t) (*s - '1')] = 1.0f;
            for (int i = 0; i < FmEngine::numOps; ++i)
                alg.fb[(size_t) i][(size_t) i] = 1.0f;
            if (d.fbSrc != d.fbOp)
            {
                alg.fb[(size_t) d.fbOp - 1][(size_t) d.fbOp - 1] = 0.0f;
                alg.fb[(size_t) d.fbOp - 1][(size_t) d.fbSrc - 1] = 1.0f;
            }
            alg.feedbackOp = d.fbOp - 1;
        }
    }
};
const AlgTable& algTable() { static const AlgTable t; return t; }

FmEngine::Algorithm buildCustom (const EngineVoiceParams& p) noexcept
{
    FmEngine::Algorithm a;
    static constexpr const std::array<lp::Id, FmEngine::numOps>* from[FmEngine::numOps] {
        &lp::fmCustomFrom1, &lp::fmCustomFrom2, &lp::fmCustomFrom3, &lp::fmCustomFrom4, &lp::fmCustomFrom5, &lp::fmCustomFrom6 };
    for (size_t d = 0; d < (size_t) FmEngine::numOps; ++d)
    {
        for (size_t s = 0; s < (size_t) FmEngine::numOps; ++s)
            a.conn[d][s] = engineParam (p, (*from[s])[d]);
        a.fb[d][d] = 1.0f;
        a.carrier[d] = engineParam (p, lp::fmCustomOut[d]);
    }
    return a;
}

EnvParams opEnv (const EngineVoiceParams& p, size_t i) noexcept
{
    EnvParams e;
    e.attack = engineParam (p, lp::fmOpAttack[i]);
    e.hold = engineParam (p, lp::fmOpHold[i]);
    e.decay = engineParam (p, lp::fmOpDecay[i]);
    e.sustain = engineParam (p, lp::fmOpSustain[i]);
    e.release = engineParam (p, lp::fmOpRelease[i]);
    e.attackCurve = engineChoice (p, lp::fmOpAttackCurve[i]);
    e.decayCurve = engineChoice (p, lp::fmOpDecayCurve[i]);
    e.releaseCurve = engineChoice (p, lp::fmOpReleaseCurve[i]);
    e.keyTrack = engineParam (p, lp::fmOpRateScale[i]);
    e.loop = engineSwitch (p, lp::fmOpLoop[i]);
    e.timeScale = engineParam (p, lp::fmOpTimeScale[i]);
    return e;
}

EnvParams pitchEnvParams (const EngineVoiceParams& p) noexcept
{
    EnvParams e;
    e.attack = engineParam (p, lp::fmPenvAttack);
    e.decay = engineParam (p, lp::fmPenvDecay);
    e.sustain = engineParam (p, lp::fmPenvSustain);
    e.release = engineParam (p, lp::fmPenvRelease);
    e.attackCurve = engineChoice (p, lp::fmPenvAttackCurve);
    e.decayCurve = engineChoice (p, lp::fmPenvDecayCurve);
    e.releaseCurve = engineChoice (p, lp::fmPenvReleaseCurve);
    e.velocity = engineParam (p, lp::fmPenvVelocity);
    e.keyTrack = engineParam (p, lp::fmPenvKeytrack);
    e.retrigger = engineSwitch (p, lp::fmPenvRetrigger);
    return e;
}
} // namespace

// ------------------------------------------------------------------ static helpers
const FmEngine::Algorithm& FmEngine::classic (int index) noexcept
{
    return algTable().a[(size_t) std::clamp (index, 0, numAlgorithms - 1)];
}

float FmEngine::levelToAmp (float level) noexcept
{
    return level <= 0.0f ? 0.0f : std::exp2 (-12.0f * (1.0f - std::min (level, 1.0f)));
}

float FmEngine::keyScale (float level, int note, int bp, float ld, float rd, int lc, int rc) noexcept
{
    if (level <= 0.0f || note == bp)
        return level;
    const bool left = note < bp;
    const float depth = left ? ld : rd;
    const int curve = left ? lc : rc;
    if (depth <= 0.0f)
        return level;
    const float oct = (float) std::abs (note - bp) / 12.0f;
    const bool expo = curve == 1 || curve == 2;
    const float f = std::min (1.0f, expo ? (std::exp2 (oct) - 1.0f) / 15.0f : oct / 4.0f);
    const float sign = curve >= 2 ? 1.0f : -1.0f;
    return std::clamp (level + sign * depth * f, 0.0f, 1.0f);
}

float FmEngine::wave (int type, double ph) noexcept
{
    const float f = frac (ph);
    switch (type)
    {
        case 1: return f < 0.5f ? sin01 (f) : 0.0f;                               // Half Sine
        case 2: return f < 0.5f ? sin01 (f) : -sin01 (f);                         // Abs Sine
        case 3: return f < 0.25f ? sin01 (f) : (f >= 0.5f && f < 0.75f) ? sin01 (f - 0.5f) : 0.0f; // Pulse Sine
        case 4: return f < 0.5f ? sin01 (2.0f * f) : 0.0f;                        // Alt Sine
        case 5: { const float g = 2.0f * f; return f < 0.5f ? (g < 0.5f ? sin01 (g) : -sin01 (g)) : 0.0f; } // Camel
        case 6: return std::clamp (4.0f * sin01 (f), -1.0f, 1.0f);                // soft Square
        case 7:                                                                   // soft Saw (3 partials)
        {
            const float s = sin01 (f), c = sin01 (frac ((double) f + 0.25));
            return 0.69f * (s + s * c + s * (3.0f - 4.0f * s * s) * (1.0f / 3.0f));
        }
        default: return sin01 (f);
    }
}

// ------------------------------------------------------------------ lifecycle
void FmEngine::prepare (double sampleRate, int maxBlockSize, int maxVoices)
{
    sr = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    const int maxN = maxBlock * maxOversampling;
    voices.assign ((size_t) std::max (1, maxVoices), VoiceState {});
    busL.assign ((size_t) maxN + 8, 0.0f);
    busR.assign ((size_t) maxN + 8, 0.0f);
    work.assign ((size_t) Decimator::workSize (maxN) + 8, 0.0f);
    (void) sineTable();
    (void) algTable();
    (void) Halfband::longStage();
    (void) Halfband::shortStage();
    reset();
}

void FmEngine::reset()
{
    for (size_t v = 0; v < voices.size(); ++v)
        kill ((int) v);
}

void FmEngine::kill (int voice)
{
    auto& vs = voices[(size_t) voice];
    vs = VoiceState {};
    for (auto& e : vs.env) e.setSampleRate (sr);
    vs.pitchEnv.setSampleRate (sr);
}

void FmEngine::setOversampling (int factor)
{
    const int f = factor >= 8 ? 8 : factor >= 4 ? 4 : factor >= 2 ? 2 : 1;
    if (f == os)
        return;
    os = f;
    for (auto& vs : voices)
        for (auto& d : vs.dec) d.reset();
}

void FmEngine::noteOn (int voice, const EngineNote& n)
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

void FmEngine::noteOff (int voice)
{
    voices[(size_t) voice].pendingRelease = true;
}

void FmEngine::start (VoiceState& vs, const EngineVoiceParams& p, bool retrigger) noexcept
{
    for (size_t i = 0; i < (size_t) numOps; ++i)
    {
        vs.env[i].setSampleRate (sr);
        vs.env[i].noteOn (opEnv (p, i), vs.velocity, vs.note);
        if (retrigger && engineSwitch (p, lp::fmOpReset[i]))
        {
            const double ph = engineParam (p, lp::fmOpPhase[i]) / 360.0;
            for (auto& u : vs.phase) u[i] = ph;
        }
    }
    vs.pitchEnv.setSampleRate (sr);
    vs.pitchEnv.noteOn (pitchEnvParams (p), vs.velocity, vs.note);
    if (! vs.started)
    {
        for (auto& h : vs.h1) h.fill (0.0f);
        for (auto& h : vs.h2) h.fill (0.0f);
        vs.fundPhase.fill (0.0);
    }
}

// ------------------------------------------------------------------ control rate
void FmEngine::computeTargets (VoiceState& vs, const EngineVoiceParams& p, int n, float pitch0, float pitch1,
                               Targets& t) noexcept
{
    // Algorithm A/B with morph
    const int ia = std::clamp (engineChoice (p, lp::fmAlgorithm), 0, customAlgorithm);
    const int ib = std::clamp (engineChoice (p, lp::fmAlgorithmB), 0, customAlgorithm);
    const float m = std::clamp (engineParam (p, lp::fmMorph), 0.0f, 1.0f);
    Algorithm custom;
    if (ia == customAlgorithm || (ib == customAlgorithm && m > 0.0f))
        custom = buildCustom (p);
    const Algorithm& A = ia == customAlgorithm ? custom : classic (ia);
    const Algorithm& B = ib == customAlgorithm ? custom : classic (ib);
    const float ma = m > 0.0f ? 1.0f - m : 1.0f, mb = m > 0.0f ? m : 0.0f;

    // LFO 1/2 depths
    float lfoPitch = 0.0f, opMod = 0.0f, ampDepth = 0.0f, ampLfo = 0.0f;
    for (size_t l = 0; l < 2; ++l)
    {
        const float v = p.lfo != nullptr ? p.lfo[l] : 0.0f;
        lfoPitch += engineParam (p, lp::fmLfoPitch[l]) * v;
        opMod += engineParam (p, lp::fmLfoOp[l]) * v;
        const float ad = engineParam (p, lp::fmLfoAmp[l]);
        ampDepth += ad;
        ampLfo += ad * 0.5f * (1.0f - v);
    }
    (void) ampDepth;
    const float modIndex = engineParam (p, lp::fmModIndex) * std::max (0.0f, 1.0f + opMod);

    std::array<float, numOps> carrier {};
    for (size_t d = 0; d < (size_t) numOps; ++d)
    {
        carrier[d] = ma * A.carrier[d] + mb * B.carrier[d];
        const float fbp = engineParam (p, lp::fmOpFeedback[d]);
        for (size_t s = 0; s < (size_t) numOps; ++s)
        {
            const float c = ma * A.conn[d][s] + mb * B.conn[d][s];
            const float f = ma * A.fb[d][s] + mb * B.fb[d][s];
            t.w[d][s] = c * (d == s ? fbScale : modScale * modIndex) + fbScale * fbp * f;
        }
    }

    // Operator amplitudes
    for (size_t i = 0; i < (size_t) numOps; ++i)
    {
        const float e = vs.env[i].advance (opEnv (p, i), n);
        float amp = 0.0f;
        if (engineSwitch (p, lp::fmOpOn[i]))
        {
            float level = engineParam (p, lp::fmOpLevel[i]);
            level = keyScale (level, vs.note, engineChoice (p, lp::fmOpKsBreak[i]),
                              engineParam (p, lp::fmOpKsLeftDepth[i]), engineParam (p, lp::fmOpKsRightDepth[i]),
                              engineChoice (p, lp::fmOpKsLeftCurve[i]), engineChoice (p, lp::fmOpKsRightCurve[i]));
            if (level > 0.0f)
                level = std::max (0.0f, level - engineParam (p, lp::fmOpVelSens[i]) * (1.0f - vs.velocity) * 0.583f);
            const float trem = std::max (0.0f, 1.0f - engineParam (p, lp::fmOpAmSens[i]) * ampLfo);
            amp = levelToAmp (level) * e * trem;
        }
        t.amp[i] = amp;
        t.wave[i] = std::clamp (engineChoice (p, lp::fmOpWave[i]), 0, 7);
        t.sync[i] = engineSwitch (p, lp::fmOpSync[i]);
    }
    // An operator runs when it has amplitude and someone listens (carrier or a link to another op).
    for (size_t i = 0; i < (size_t) numOps; ++i)
    {
        bool used = carrier[i] > 0.0f;
        for (size_t d = 0; d < (size_t) numOps && ! used; ++d)
            used = d != i && (t.w[d][i] != 0.0f || vs.w[d][i] != 0.0f);
        t.on[i] = used && (t.amp[i] > 0.0f || vs.amp[i] > 0.0f);
    }

    // Pitch
    const float penv = engineParam (p, lp::fmPenvAmount);
    float pe = 0.0f;
    {
        EnvParams pp = pitchEnvParams (p);
        vs.pitchEnv.advance (pp, n);
        pe = penv != 0.0f ? penv * vs.pitchEnv.output (pp) : 0.0f;
    }
    const float common = pe + lfoPitch;
    const double rate = sr * os;
    t.unison = std::clamp (p.unison, 1, maxUnison);
    const float uNorm = 1.0f / std::sqrt ((float) t.unison);
    const float out = engineParam (p, lp::fmOutput);
    for (int u = 0; u < t.unison; ++u)
    {
        const auto uu = (size_t) u;
        const float posU = t.unison > 1 ? (float) u / (float) (t.unison - 1) - 0.5f : 0.0f;
        float cents = posU * p.detuneCents;
        if (p.driftSemis != nullptr)
            cents += p.driftSemis[uu] * 100.0f;
        const float panU = p.unisonPan + 2.0f * posU * p.spread;
        const float uSemis = cents * 0.01f;
        t.fund0[uu] = (float) std::min ((double) maxInc, noteHz (pitch0 + common + uSemis) / rate);
        t.fund1[uu] = (float) std::min ((double) maxInc, noteHz (pitch1 + common + uSemis) / rate);
        for (size_t i = 0; i < (size_t) numOps; ++i)
        {
            const float off = (float) engineChoice (p, lp::fmOpCoarse[i])
                            + (engineParam (p, lp::fmOpFine[i]) + engineParam (p, lp::fmOpDetune[i])) * 0.01f + uSemis;
            float f0, f1;
            if (engineChoice (p, lp::fmOpMode[i]) == 1)
                f0 = f1 = engineParam (p, lp::fmOpFixed[i]) * std::exp2 (off / 12.0f);
            else
            {
                const float r = engineParam (p, lp::fmOpRatio[i]);
                f0 = noteHz (pitch0 + common + off) * r;
                f1 = noteHz (pitch1 + common + off) * r;
            }
            t.inc0[uu][i] = (float) std::min ((double) maxInc, f0 / rate);
            t.inc1[uu][i] = (float) std::min ((double) maxInc, f1 / rate);
            float gl, gr;
            panGains (engineParam (p, lp::fmOpPan[i]) + panU, gl, gr);
            const float g = carrier[i] * out * uNorm;
            t.cgL[uu][i] = g * gl;
            t.cgR[uu][i] = g * gr;
        }
    }
}

// ------------------------------------------------------------------ audio
bool FmEngine::render (int voice, const EngineVoiceParams& p, float* left, float* right, int n) noexcept
{
    auto& vs = voices[(size_t) voice];
    for (int done = 0; done < n;)
    {
        const int m = std::min (maxBlock, n - done);
        const float a0 = (float) done / (float) n, a1 = (float) (done + m) / (float) n;
        renderBlock (vs, p, left + done, right + done, m,
                     p.pitchStart + (p.pitchEnd - p.pitchStart) * a0, p.pitchStart + (p.pitchEnd - p.pitchStart) * a1);
        done += m;
    }
    return false;
}

void FmEngine::renderBlock (VoiceState& vs, const EngineVoiceParams& p, float* left, float* right, int n,
                            float pitch0, float pitch1) noexcept
{
    bool first = false;
    if (vs.pendingStart)
    {
        start (vs, p, ! vs.legato || ! vs.started);
        first = ! vs.started;
        vs.started = true;
        vs.pendingStart = false;
        vs.legato = false;
    }
    if (vs.pendingRelease)
    {
        for (size_t i = 0; i < (size_t) numOps; ++i)
            vs.env[i].noteOff (opEnv (p, i));
        vs.pitchEnv.noteOff (pitchEnvParams (p));
        vs.pendingRelease = false;
    }

    auto& t = tg;
    computeTargets (vs, p, n, pitch0, pitch1, t);
    if (first)
    {
        vs.w = t.w;
        vs.cgL = t.cgL;
        vs.cgR = t.cgR;
    }
    if (t.unison != vs.unison)
    {
        for (int u = vs.unison; u < t.unison; ++u)   // new unison copies start in phase with copy 0
        {
            vs.phase[(size_t) u] = vs.phase[0];
            vs.h1[(size_t) u].fill (0.0f);
            vs.h2[(size_t) u].fill (0.0f);
            vs.fundPhase[(size_t) u] = vs.fundPhase[0];
        }
        vs.unison = t.unison;
    }

    const int N = n * os, U = t.unison;
    const float invN = 1.0f / (float) N;

    // Active operators and their non-zero links (processed from op 6 down to op 1).
    std::array<int, numOps> ops {};
    int numActive = 0;
    for (int i = numOps - 1; i >= 0; --i)
        if (t.on[(size_t) i])
            ops[(size_t) numActive++] = i;

    std::fill (busL.begin(), busL.begin() + N, 0.0f);
    std::fill (busR.begin(), busR.begin() + N, 0.0f);

    if (numActive > 0)
    {
        struct Link { int src; float w, dw; };
        std::array<std::array<Link, numOps>, numOps> links {};
        std::array<int, numOps> numLinks {};
        std::array<float, numOps> amp {}, damp {};
        for (int k = 0; k < numActive; ++k)
        {
            const auto d = (size_t) ops[(size_t) k];
            amp[d] = vs.amp[d];
            damp[d] = (t.amp[d] - vs.amp[d]) * invN;
            for (int kk = 0; kk < numActive; ++kk)
            {
                const auto s = (size_t) ops[(size_t) kk];
                if (vs.w[d][s] != 0.0f || t.w[d][s] != 0.0f)
                    links[d][(size_t) numLinks[d]++] = { (int) s, vs.w[d][s], (t.w[d][s] - vs.w[d][s]) * invN };
            }
        }
        std::array<std::array<float, numOps>, maxUnison> cl {}, cr {}, dcl {}, dcr {}, inc {}, dinc {};
        std::array<float, maxUnison> finc {}, dfinc {};
        bool anySync = false;
        for (int u = 0; u < U; ++u)
        {
            const auto uu = (size_t) u;
            for (size_t i = 0; i < (size_t) numOps; ++i)
            {
                cl[uu][i] = vs.cgL[uu][i]; dcl[uu][i] = (t.cgL[uu][i] - vs.cgL[uu][i]) * invN;
                cr[uu][i] = vs.cgR[uu][i]; dcr[uu][i] = (t.cgR[uu][i] - vs.cgR[uu][i]) * invN;
                inc[uu][i] = t.inc0[uu][i]; dinc[uu][i] = (t.inc1[uu][i] - t.inc0[uu][i]) * invN;
                anySync = anySync || (t.sync[i] && t.on[i]);
            }
            finc[uu] = t.fund0[uu];
            dfinc[uu] = (t.fund1[uu] - t.fund0[uu]) * invN;
        }

        for (int s = 0; s < N; ++s)
        {
            float sl = 0.0f, sr_ = 0.0f;
            for (int u = 0; u < U; ++u)
            {
                const auto uu = (size_t) u;
                auto& ph = vs.phase[uu];
                auto& h1 = vs.h1[uu];
                auto& h2 = vs.h2[uu];
                if (anySync)
                {
                    double& fp = vs.fundPhase[uu];
                    fp += (double) finc[uu];
                    if (fp >= 1.0)
                    {
                        fp -= std::floor (fp);
                        const double since = fp / std::max (1.0e-9, (double) finc[uu]);
                        for (size_t i = 0; i < (size_t) numOps; ++i)
                            if (t.sync[i])
                                ph[i] = since * (double) inc[uu][i];
                    }
                }
                std::array<float, numOps> cur = h1;
                for (int k = 0; k < numActive; ++k)
                {
                    const int d = ops[(size_t) k];
                    const auto dd = (size_t) d;
                    float mod = 0.0f;
                    for (int l = 0; l < numLinks[dd]; ++l)
                    {
                        const auto& lk = links[dd][(size_t) l];
                        const float x = lk.src == d ? 0.5f * (h1[dd] + h2[dd]) : cur[(size_t) lk.src];
                        mod += lk.w * x;
                    }
                    const float y = wave (t.wave[dd], ph[dd] + (double) mod) * amp[dd];
                    cur[dd] = y;
                    ph[dd] += (double) inc[uu][dd];
                    if (ph[dd] >= 1.0) ph[dd] -= std::floor (ph[dd]);
                    sl += y * cl[uu][dd];
                    sr_ += y * cr[uu][dd];
                }
                h2 = h1;
                h1 = cur;
                for (size_t i = 0; i < (size_t) numOps; ++i)
                {
                    cl[uu][i] += dcl[uu][i];
                    cr[uu][i] += dcr[uu][i];
                    inc[uu][i] += dinc[uu][i];
                }
                finc[uu] += dfinc[uu];
            }
            busL[(size_t) s] = sl;
            busR[(size_t) s] = sr_;
            for (int k = 0; k < numActive; ++k)
            {
                const auto d = (size_t) ops[(size_t) k];
                amp[d] += damp[d];
                for (int l = 0; l < numLinks[d]; ++l)
                    links[d][(size_t) l].w += links[d][(size_t) l].dw;
            }
        }
        // inactive operators output silence (no stale values for delayed links)
        for (size_t i = 0; i < (size_t) numOps; ++i)
            if (! t.on[i])
                for (int u = 0; u < U; ++u) { vs.h1[(size_t) u][i] = 0.0f; vs.h2[(size_t) u][i] = 0.0f; }
    }
    else
    {
        for (auto& h : vs.h1) h.fill (0.0f);
        for (auto& h : vs.h2) h.fill (0.0f);
    }

    vs.amp = t.amp;
    vs.w = t.w;
    vs.cgL = t.cgL;
    vs.cgR = t.cgR;

    vs.dec[0].process (busL.data(), left, n, os, work.data());
    vs.dec[1].process (busR.data(), right, n, os, work.data());
}
} // namespace chroma
