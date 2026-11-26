#include "AdditiveEngine.h"
#include "engines/EngineParams.h"
#include <algorithm>
#include <cmath>

namespace chroma
{
using namespace add;

namespace
{
constexpr float dbPerOctToLog2 = 1.0f / 6.0206f;

inline float clampf (float x, float lo, float hi) noexcept { return x < lo ? lo : x > hi ? hi : x; }
inline float noteHz (float pitch) noexcept { return 440.0f * std::exp2 ((pitch - 69.0f) / 12.0f); }

inline uint32_t xorshift (uint32_t& s) noexcept
{
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}

EnvParams spectralEnv (const EngineVoiceParams& p) noexcept
{
    EnvParams e;
    e.attack = engineParam (p, lp::addSenvAttack);
    e.decay = engineParam (p, lp::addSenvDecay);
    e.sustain = engineParam (p, lp::addSenvSustain);
    e.release = engineParam (p, lp::addSenvRelease);
    e.attackCurve = 0;
    e.decayCurve = e.releaseCurve = engineChoice (p, lp::addSenvCurve);
    return e;
}

// Polynomial band-limited step correction (2-sample PolyBLEP), t = phase in cycles, dt = increment.
inline float polyBlep (float t, float dt) noexcept
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

// One partial over one sub-block: phase = ph0 + Σ increments (linear glide) + FM prefix sum; the
// L/R amplitudes ramp linearly. Written for auto-vectorisation along samples.
inline void oscillate (float ph0, float inc0, float dinc, float dev, const float* __restrict I, const float* __restrict T2,
                       const float* __restrict M, float l0, float dl, float r0, float dr,
                       float* __restrict L, float* __restrict R, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const float v = sin2piReduced (ph0 + inc0 * I[i] + dinc * T2[i] + dev * M[i]);
        L[i] += (l0 + dl * I[i]) * v;
        R[i] += (r0 + dr * I[i]) * v;
    }
}

inline void oscillateNoFm (float ph0, float inc0, float dinc, const float* __restrict I, const float* __restrict T2,
                           float l0, float dl, float r0, float dr, float* __restrict L, float* __restrict R, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const float v = sin2piReduced (ph0 + inc0 * I[i] + dinc * T2[i]);
        L[i] += (l0 + dl * I[i]) * v;
        R[i] += (r0 + dr * I[i]) * v;
    }
}
} // namespace

// ------------------------------------------------------------------ envelopes
float AdditiveEngine::envShape (float x, int curve) noexcept
{
    constexpr float norm = 1.0f / (1.0f - 0.006737947f);   // same laws as Envelope (k = 5)
    constexpr float k = 5.0f * 1.4426950f;                  // in log2 units
    switch (curve)
    {
        case 1:  return (1.0f - fastExp2 (-k * x)) * norm;
        case 2:  return (fastExp2 (k * (x - 1.0f)) - 0.006737947f) * norm;
        default: return x;
    }
}

float AdditiveEngine::partialEnvHeld (float ts, float attack, float decay, float sustain, int curve) noexcept
{
    attack = std::max (attack, 1.0e-4f);
    if (ts < attack)
        return envShape (ts / attack, curve == 1 ? 0 : curve);   // exponential attack = linear (as Envelope's default)
    ts -= attack;
    decay = std::max (decay, 1.0e-4f);
    if (ts < decay)
        return 1.0f + (sustain - 1.0f) * envShape (ts / decay, curve);
    return sustain;
}

// ------------------------------------------------------------------ lifecycle
void AdditiveEngine::prepare (double sampleRate, int, int maxVoices)
{
    sr = sampleRate;
    cacheValid = false;
    (void) shapeTable (0);   // build factory shapes off the audio thread
    voices.assign ((size_t) std::max (1, maxVoices), VoiceState {});
    for (size_t v = 0; v < voices.size(); ++v)
    {
        voices[v].phase.assign ((size_t) maxUnison * maxPartials, 0.0f);
        voices[v].rng = 0x9e3779b9u * (uint32_t) (v + 1);
    }
    for (int k = 0; k < maxPartials; ++k)
    {
        idxLog2[(size_t) k] = std::log2 ((float) (k + 1));
        const float h = std::sin ((float) (k + 1) * 12.9898f) * 43758.5453f;
        density[(size_t) k] = k == 0 ? -1.0f : h - std::floor (h);   // fundamental always kept
    }
    for (int i = 0; i <= subBlock; ++i)
    {
        rampI[(size_t) i] = (float) i;
        rampT2[(size_t) i] = 0.5f * (float) i * (float) (i - 1);
    }
    reset();
}

void AdditiveEngine::reset()
{
    for (size_t v = 0; v < voices.size(); ++v)
        kill ((int) v);
}

void AdditiveEngine::kill (int voice)
{
    auto& vs = voices[(size_t) voice];
    vs.active = vs.started = vs.pendingStart = vs.pendingRelease = vs.released = vs.legato = false;
    vs.lastCount = 0;
    vs.senv.kill();
    vs.gL.fill (0.0f);
    vs.gR.fill (0.0f);
}

void AdditiveEngine::noteOn (int voice, const EngineNote& n)
{
    auto& vs = voices[(size_t) voice];
    vs.note = n.note;
    vs.active = true;
    if (n.retrigger || ! vs.started)
    {
        vs.pendingStart = true;
        vs.pendingRelease = false;
        vs.legato = ! n.retrigger;
    }
}

void AdditiveEngine::noteOff (int voice) { voices[(size_t) voice].pendingRelease = true; }

float AdditiveEngine::randomPhase (VoiceState& vs) noexcept
{
    return (float) (xorshift (vs.rng) >> 8) * (1.0f / 16777216.0f);
}

void AdditiveEngine::start (VoiceState& vs, const EngineVoiceParams& p, bool retrigger) noexcept
{
    if (! retrigger)
        return;   // legato: keep phases and envelopes
    vs.senv.setSampleRate (sr);
    vs.senv.noteOn (spectralEnv (p), 1.0f, vs.note);
    vs.released = vs.relCaptured = false;
    vs.t = 0.0;
    vs.fmPhase = vs.amPhase = 0.0;
    vs.fmY1 = vs.fmY2 = 0.0f;
    const bool reset = engineSwitch (p, lp::addPhaseReset);
    const float morph = engineParam (p, lp::addMorph);
    const auto& a = specA;   // filled by buildSpectrum just before
    const auto& b = specB;
    for (int k = 0; k < maxPartials; ++k)
        vs.phase[(size_t) k] = reset ? a.at (fPhase, k) + (b.at (fPhase, k) - a.at (fPhase, k)) * morph : randomPhase (vs);
    for (int u = 1; u < maxUnison; ++u)
        for (int k = 0; k < maxPartials; ++k)
            vs.phase[(size_t) (u * maxPartials + k)] = randomPhase (vs);
}

// ------------------------------------------------------------------ spectrum
void AdditiveEngine::buildSpectrum (const EngineVoiceParams& p, int count, int shapeCount, float morph, float extraTilt) noexcept
{
    const int shA = engineChoice (p, lp::addSpectrumA), shB = engineChoice (p, lp::addSpectrumB);

    // Voices usually share the same values: rebuild only when an input changed.
    const auto ver = [&] (int which) { return custom[(size_t) which] != nullptr ? custom[(size_t) which]->version() : 0u; };
    const std::array<float, 24> in { (float) shA, (float) shB, (float) count, (float) shapeCount, morph, extraTilt,
        engineParam (p, lp::addDistribution), engineParam (p, lp::addStretch), engineParam (p, lp::addInharm),
        engineParam (p, lp::addTilt), engineParam (p, lp::addSlope), engineParam (p, lp::addBrightness),
        engineParam (p, lp::addOdd), engineParam (p, lp::addEven), engineParam (p, lp::addBalance),
        engineParam (p, lp::addDensity), engineParam (p, lp::addShift), engineParam (p, lp::addOutput),
        engineParam (p, lp::addWidth), engineParam (p, lp::addSpread), engineParam (p, lp::addStereoPos),
        engineParam (p, lp::addFmDepth), std::bit_cast<float> (ver (0)), std::bit_cast<float> (ver (1)) };
    std::array<uint32_t, 24> key {};
    for (size_t i = 0; i < key.size(); ++i) key[i] = std::bit_cast<uint32_t> (in[i]);
    if (cacheValid && key == cacheKey)
        return;
    cacheKey = key;
    cacheValid = true;
    auto fetch = [&] (int shape, int which, SpectrumData& d)
    {
        if (shape == Custom && custom[(size_t) which] != nullptr)
            custom[(size_t) which]->copyTo (d, count);
        else
            for (int f = 0; f < numFields; ++f)
                std::copy_n (shapeTable (shape == Custom ? Saw : shape).v[(size_t) f].begin(), count, d.v[(size_t) f].begin());
    };
    fetch (shA, 0, specA);
    fetch (shB, 1, specB);

    // Mute / Solo / Polarity → signed amplitude
    for (auto* d : { &specA, &specB })
    {
        bool solo = false;
        for (int k = 0; k < count; ++k) solo = solo || d->at (fSolo, k) > 0.5f;
        for (int k = 0; k < count; ++k)
        {
            const bool on = d->at (fMute, k) < 0.5f && (! solo || d->at (fSolo, k) > 0.5f);
            d->at (fAmp, k) = on ? d->at (fAmp, k) * d->at (fPolarity, k) : 0.0f;
        }
    }

    const int dist = engineChoice (p, lp::addDistribution);
    const float stretch = 1.0f + 0.1f * engineParam (p, lp::addStretch);
    const float inh = engineParam (p, lp::addInharm), B = 0.002f * inh * inh;
    const float m = clampf (morph, 0.0f, 1.0f);
    const float* aR = specA.v[fRatio].data(); const float* bR = specB.v[fRatio].data();
    const float* aF = specA.v[fFreq].data();  const float* bF = specB.v[fFreq].data();
    const float* aA = specA.v[fAmp].data();   const float* bA = specB.v[fAmp].data();
    const float* aP = specA.v[fPan].data();   const float* bP = specB.v[fPan].data();
    const float* aD = specA.v[fDetune].data(); const float* bD = specB.v[fDetune].data();
    const float* aN = specA.v[fFine].data();  const float* bN = specB.v[fFine].data();
    for (int k = 0; k < count; ++k)
    {
        const auto kk = (size_t) k;
        float r = dist == 1 ? (float) (k + 1) : dist == 2 ? (float) (2 * k + 1)
                : dist == 3 ? fastExp2 ((float) k) : aR[k] + (bR[k] - aR[k]) * m;
        if (stretch != 1.0f) r = fastExp2 (fastLog2 (r) * stretch);
        if (B > 0.0f) r *= std::sqrt (1.0f + B * r * r);
        const float fixedHz = aF[k] + (bF[k] - aF[k]) * m;
        sBase[kk] = fixedHz > 0.0f ? 0.0f : r;
        sFixed[kk] = fixedHz;
        sAmp[kk] = aA[k] + (bA[k] - aA[k]) * m;
        sPan[kk] = aP[k] + (bP[k] - aP[k]) * m;
        sOffHz[kk] = aD[k] + (bD[k] - aD[k]) * m;
        sFineMul[kk] = fastExp2 ((aN[k] + (bN[k] - aN[k]) * m) * (1.0f / 1200.0f));
    }

    // Global amplitude shaping in log2 domain (ratios relative to the fundamental).
    const float tilt = (engineParam (p, lp::addTilt) + extraTilt) * dbPerOctToLog2;
    const float slope = engineParam (p, lp::addSlope) * dbPerOctToLog2;
    const float cut = 8.0f * engineParam (p, lp::addBrightness);
    const float odd = engineParam (p, lp::addOdd), even = engineParam (p, lp::addEven);
    const float bal = engineParam (p, lp::addBalance);
    const float dens = engineParam (p, lp::addDensity) * 1.05f;
    const float balStep = shapeCount > 1 ? 2.0f / (float) (shapeCount - 1) : 0.0f;
    for (int k = 0; k < count; ++k)
    {
        const auto kk = (size_t) k;
        const float lr = fastLog2 (std::max (sBase[kk], 1.0e-3f));   // fixed partials: shaped by index
        const float l = sBase[kk] > 0.0f ? lr : idxLog2[kk];
        sLr[kk] = l;
        float g2 = tilt * l;
        if (l > cut) g2 -= slope * (l - cut);
        float g = fastExp2 (g2);
        g *= k == 0 ? 1.0f : ((k & 1) ? even : odd);   // k = 1 → partial 2 (even)
        g *= clampf (1.0f + bal * ((float) k * balStep - 1.0f), 0.0f, 2.0f);
        g *= clampf ((dens - density[kk]) * 20.0f, 0.0f, 1.0f);
        sAmp[kk] *= g;
    }

    // Derived per-partial values used at control rate by every voice.
    const float out = engineParam (p, lp::addOutput) * outputScale;
    const float shift = engineParam (p, lp::addShift);
    const float fmDepth = engineParam (p, lp::addFmDepth);
    const float width = engineParam (p, lp::addWidth), spread = engineParam (p, lp::addSpread);
    const float spos = engineParam (p, lp::addStereoPos);
    const float posStep = shapeCount > 1 ? 2.0f / (float) (shapeCount - 1) : 0.0f;
    for (int k = 0; k < count; ++k)
    {
        const auto kk = (size_t) k;
        sAmp[kk] *= out;
        sRf[kk] = sFineMul[kk] * sBase[kk];
        sFx[kk] = sFineMul[kk] * sFixed[kk] + sOffHz[kk] + shift;
        sDevW[kk] = fastExp2 (fmDepth * sLr[kk]);
        float pan = sPan[kk] + spos * (1.0f - (float) k * posStep);   // + = low partials right
        if (k > 0) pan += (k & 1) ? spread : -spread;
        sPan[kk] = clampf (pan, -1.0f, 1.0f);
        const float pw = sPan[kk] * width;
        sPanL[kk] = std::sqrt (1.0f - pw);
        sPanR[kk] = std::sqrt (1.0f + pw);
    }
}

// ------------------------------------------------------------------ audio
bool AdditiveEngine::render (int voice, const EngineVoiceParams& p, float* left, float* right, int n) noexcept
{
    auto& vs = voices[(size_t) voice];
    if (! vs.active)
    {
        std::fill_n (left, n, 0.0f);
        std::fill_n (right, n, 0.0f);
        return false;
    }
    for (int done = 0; done < n;)
    {
        const int m = std::min (subBlock, n - done);
        const float a0 = (float) done / (float) n, a1 = (float) (done + m) / (float) n;
        renderBlock (vs, p, left + done, right + done, m,
                     p.pitchStart + (p.pitchEnd - p.pitchStart) * a0, p.pitchStart + (p.pitchEnd - p.pitchStart) * a1);
        done += m;
    }
    return false;
}

void AdditiveEngine::renderBlock (VoiceState& vs, const EngineVoiceParams& p, float* left, float* right, int n,
                                  float pitch0, float pitch1) noexcept
{
    const int count = std::clamp (engineChoice (p, lp::addPartials), 1, maxPartials);
    const float fs = (float) sr;
    const float dt = (float) n / fs;

    // Spectral ENV (control rate)
    const EnvParams sep = spectralEnv (p);
    bool first = false;
    if (vs.pendingStart)
    {
        buildSpectrum (p, maxPartials, maxPartials, engineParam (p, lp::addMorph), 0.0f);
        start (vs, p, ! vs.legato || ! vs.started);
        first = ! vs.started;
        vs.started = true;
        vs.pendingStart = false;
        vs.legato = false;
    }
    if (vs.pendingRelease)
    {
        vs.senv.noteOff (sep);
        vs.pendingRelease = false;
        vs.released = true;
        vs.relCaptured = false;
        vs.tRel = vs.t;
    }
    const float e = vs.senv.advance (sep, n);
    const float senvAmt = engineParam (p, lp::addSenvAmount) * e;
    const float morph = engineParam (p, lp::addMorph) + senvAmt * engineParam (p, lp::addSenvPosition);
    const int built = std::max (count, vs.lastCount);   // partials removed this block still need to fade out
    vs.lastCount = count;
    buildSpectrum (p, built, count, morph, senvAmt * engineParam (p, lp::addSenvTilt));

    // Pitch / unison
    const int U = std::clamp (p.unison, 1, maxUnison);
    if (U > vs.unison)
        for (int u = vs.unison; u < U; ++u)
            for (int k = 0; k < maxPartials; ++k)
                vs.phase[(size_t) (u * maxPartials + k)] = randomPhase (vs);
    vs.unison = U;
    const float f0a = noteHz (pitch0), f0b = noteHz (pitch1), f0max = std::max (f0a, f0b);
    float maxDetune = 1.0f;
    std::array<float, maxUnison> uMul {}, uL {}, uR {};
    const float uNorm = 1.0f / std::sqrt ((float) U);
    for (int u = 0; u < U; ++u)
    {
        const float posU = U > 1 ? (float) u / (float) (U - 1) - 0.5f : 0.0f;
        float cents = posU * p.detuneCents;
        if (p.driftSemis != nullptr) cents += p.driftSemis[u] * 100.0f;
        uMul[(size_t) u] = std::exp2 (cents / 1200.0f);
        maxDetune = std::max (maxDetune, uMul[(size_t) u]);
        const float pan = clampf (p.unisonPan + 2.0f * posU * p.spread, -1.0f, 1.0f);
        uL[(size_t) u] = std::sqrt (1.0f - pan) * uNorm;
        uR[(size_t) u] = std::sqrt (1.0f + pan) * uNorm;
    }

    // FM modulator (shared by the partials; prefix sum of its output for the phase integrals)
    const float fmAmt = clampf (engineParam (p, lp::addFmAmount) + e * engineParam (p, lp::addFmEnv), 0.0f, 1.0f);
    const bool fmOn = fmAmt > 1.0e-5f;
    if (fmOn)
    {
        const float fmHz = 0.5f * (f0a + f0b) * engineParam (p, lp::addFmRatio) + engineParam (p, lp::addFmFreq);
        const double inc = std::min (0.5, (double) fmHz / sr);
        const float fb = engineParam (p, lp::addFmFeedback) * 0.25f;
        float acc = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            fmSum[(size_t) i] = acc;
            const float y = sin2pi ((float) vs.fmPhase + fb * (vs.fmY1 + vs.fmY2));
            vs.fmY2 = vs.fmY1;
            vs.fmY1 = y;
            acc += y;
            vs.fmPhase += inc;
            vs.fmPhase -= std::floor (vs.fmPhase);
        }
        fmSum[(size_t) n] = acc;
    }
    else
        vs.fmY1 = vs.fmY2 = 0.0f;

    // Partial ENV
    const float penvAmt = engineParam (p, lp::addPenvAmount);
    const float pA = engineParam (p, lp::addPenvAttack), pD = engineParam (p, lp::addPenvDecay);
    const float pS = engineParam (p, lp::addPenvSustain), pR = std::max (1.0e-4f, engineParam (p, lp::addPenvRelease));
    const int pCurve = engineChoice (p, lp::addPenvCurve);
    const float pScale = engineParam (p, lp::addPenvScaling), pDist = engineParam (p, lp::addPenvDist);
    if (penvAmt > 0.0f && vs.released && ! vs.relCaptured)
    {
        for (int k = 0; k < maxPartials; ++k)
            vs.relLevel[(size_t) k] = partialEnvHeld ((float) vs.tRel * fastExp2 (pScale * idxLog2[(size_t) k]), pA, pD, pS, pCurve);
        vs.relCaptured = true;
    }
    const float tEnd = (float) (vs.t + dt);

    // Targets per partial (the Nyquist test covers the highest unison copy and the FM deviation)
    const float nyqFade = 1.0f / (0.02f * fs), nyqTop = 0.47f * fs;
    const float fTopMul = f0max * maxDetune, devMul = fmOn ? fmAmt * f0max : 0.0f;
    for (int k = 0; k < count; ++k)
    {
        const auto kk = (size_t) k;
        const float fc = sRf[kk] * f0max + sFx[kk];
        const float dev = devMul * sDevW[kk];
        devHz[kk] = dev;
        const float g = clampf ((nyqTop - (sRf[kk] * fTopMul + sFx[kk] + dev)) * nyqFade, 0.0f, 1.0f);   // full up to 0.45·fs
        const float a = fc > 0.0f ? sAmp[kk] * g : 0.0f;
        tL[kk] = a * sPanL[kk];
        tR[kk] = a * sPanR[kk];
    }
    if (penvAmt > 0.0f)
        for (int k = 0; k < count; ++k)
        {
            const auto kk = (size_t) k;
            if (tL[kk] == 0.0f && tR[kk] == 0.0f) continue;
            const float x = count > 1 ? (float) k / (float) (count - 1) : 0.0f;
            const float w = pDist >= 0.0f ? 1.0f - pDist * (1.0f - x) : 1.0f + pDist * x;
            const float amt = penvAmt * w;
            const float sc = fastExp2 (pScale * idxLog2[kk]);
            const float lev = ! vs.released ? partialEnvHeld (tEnd * sc, pA, pD, pS, pCurve)
                            : vs.relLevel[kk] * (1.0f - envShape (std::min (1.0f, (tEnd - (float) vs.tRel) * sc / pR), pCurve));
            const float gEnv = 1.0f - amt + amt * lev;
            tL[kk] *= gEnv;
            tR[kk] *= gEnv;
        }
    if (const float smod = engineParam (p, lp::addStereoMod); smod > 0.0f)
    {
        const float width = engineParam (p, lp::addWidth);
        for (int k = 0; k < count; ++k)
        {
            const auto kk = (size_t) k;
            const float a = std::sqrt (0.5f * (tL[kk] * tL[kk] + tR[kk] * tR[kk]));   // undo the static pan
            const float pan = clampf (sPan[kk] + smod * sin2pi (tEnd * 0.15f * (1.0f + 0.61803f * (float) (k % 5)) + 0.618034f * (float) k),
                                      -1.0f, 1.0f) * width;
            tL[kk] = a * std::sqrt (1.0f - pan);
            tR[kk] = a * std::sqrt (1.0f + pan);
        }
    }
    for (int k = count; k < built; ++k)
    {
        tL[(size_t) k] = tR[(size_t) k] = 0.0f;
        devHz[(size_t) k] = 0.0f;
    }
    if (first)
    {
        std::copy (tL.begin(), tL.end(), vs.gL.begin());
        std::copy (tR.begin(), tR.end(), vs.gR.begin());
    }

    // AM (odd / even partial buses)
    const float amAmt = clampf (engineParam (p, lp::addAmAmount) + e * engineParam (p, lp::addAmEnv), 0.0f, 1.0f);
    const bool amOn = amAmt > 1.0e-5f;
    const float amDepth = engineParam (p, lp::addAmDepth);
    const bool split = amOn && amDepth > 0.0f;
    for (int b = 0; b < (split ? 4 : 2); ++b)
        std::fill_n (bus[(size_t) b].data(), n, 0.0f);

    // Oscillator bank
    const float inv = 1.0f / fs, invN = 1.0f / (float) n;
    const float* I = rampI.data();
    const float* T2 = rampT2.data();
    for (int k = 0; k < built; ++k)
    {
        const auto kk = (size_t) k;
        const float l0 = vs.gL[kk], r0 = vs.gR[kk], l1 = tL[kk], r1 = tR[kk];
        vs.gL[kk] = l1;
        vs.gR[kk] = r1;
        if (l0 == 0.0f && r0 == 0.0f && l1 == 0.0f && r1 == 0.0f)
            continue;
        const float dl = (l1 - l0) * invN, dr = (r1 - r0) * invN;
        float* L = bus[split && (k & 1) ? 2 : 0].data();
        float* R = bus[split && (k & 1) ? 3 : 1].data();
        const float fixedPart = sFx[kk];
        const float rf = sRf[kk];
        const float dev = devHz[kk] * inv;
        for (int u = 0; u < U; ++u)
        {
            const auto uu = (size_t) u;
            float& ph = vs.phase[(size_t) u * maxPartials + kk];
            const float inc0 = (rf * f0a * uMul[uu] + fixedPart) * inv;
            const float inc1 = (rf * f0b * uMul[uu] + fixedPart) * inv;
            const float dinc = (inc1 - inc0) * invN;
            const float gl = uL[uu], gr = uR[uu];
            if (fmOn)
                oscillate (ph, inc0, dinc, dev, I, T2, fmSum.data(), l0 * gl, dl * gl, r0 * gr, dr * gr, L, R, n);
            else
                oscillateNoFm (ph, inc0, dinc, I, T2, l0 * gl, dl * gl, r0 * gr, dr * gr, L, R, n);
            double end = (double) ph + (double) inc0 * n + (double) dinc * rampT2[(size_t) n]
                       + (fmOn ? (double) dev * fmSum[(size_t) n] : 0.0);
            end -= std::floor (end);
            ph = (float) end;
        }
    }

    // AM, Mid/Side, output
    if (amOn)
    {
        const int wave = engineChoice (p, lp::addAmWave);
        const double inc = std::min (0.5, (double) engineParam (p, lp::addAmFreq) / sr);
        const float dtf = (float) inc;
        const float evenSign = 1.0f - 2.0f * amDepth;
        for (int i = 0; i < n; ++i)
        {
            const float ph = (float) vs.amPhase;
            float m;
            switch (wave)
            {
                case 1:
                {
                    float q = ph + 0.25f; q -= (float) (int) q;
                    m = 1.0f - 4.0f * std::abs (q - 0.5f);   // triangle, sine phase
                    break;
                }
                case 2:
                {
                    float ph2 = ph + 0.5f; ph2 -= (float) (int) ph2;
                    m = (ph < 0.5f ? 1.0f : -1.0f) + polyBlep (ph, dtf) - polyBlep (ph2, dtf);
                    break;
                }
                case 3:  m = 2.0f * ph - 1.0f - polyBlep (ph, dtf); break;
                default: m = sin2pi (ph); break;
            }
            const float gOdd = 1.0f - amAmt * 0.5f * (1.0f - m);
            const float gEven = 1.0f - amAmt * 0.5f * (1.0f - m * evenSign);
            if (split)
            {
                bus[0][(size_t) i] = bus[0][(size_t) i] * gOdd + bus[2][(size_t) i] * gEven;
                bus[1][(size_t) i] = bus[1][(size_t) i] * gOdd + bus[3][(size_t) i] * gEven;
            }
            else
            {
                bus[0][(size_t) i] *= gOdd;
                bus[1][(size_t) i] *= gOdd;
            }
            vs.amPhase += inc;
            if (vs.amPhase >= 1.0) vs.amPhase -= 1.0;
        }
    }
    else if (split)
        for (int i = 0; i < n; ++i) { bus[0][(size_t) i] += bus[2][(size_t) i]; bus[1][(size_t) i] += bus[3][(size_t) i]; }

    const float msb = engineParam (p, lp::addMsBalance);
    if (msb != 0.0f)
    {
        const float gm = std::min (1.0f, 1.0f - msb), gs = std::min (1.0f, 1.0f + msb);
        for (int i = 0; i < n; ++i)
        {
            const float l = bus[0][(size_t) i], r = bus[1][(size_t) i];
            const float mid = 0.5f * (l + r) * gm, side = 0.5f * (l - r) * gs;
            left[i] = mid + side;
            right[i] = mid - side;
        }
    }
    else
    {
        std::copy_n (bus[0].data(), n, left);
        std::copy_n (bus[1].data(), n, right);
    }
    vs.t += dt;
}
} // namespace chroma
