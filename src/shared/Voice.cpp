#include "Voice.h"
#include "ParamRange.h"
#include <algorithm>
#include <cmath>

namespace chroma
{
namespace
{
inline bool on (float v) noexcept { return v > 0.5f; }
inline int idx (float v) noexcept { return (int) std::lround (v); }
}

void Voice::prepare (int voiceIndex, double sampleRate)
{
    index = voiceIndex;
    sr = sampleRate;
    seed = 0x2545F491u * (uint32_t) (voiceIndex + 1);
    for (auto& e : envs) e.setSampleRate (sampleRate);
    filter.prepare (sampleRate);
    filter2.prepare (sampleRate);
    state = State::Free;
}

float Voice::val (const LayerRuntime& L, int id) const noexcept
{
    const float o = offsets[(size_t) id];
    if (o == 0.0f)
        return L.base[(size_t) id];
    const auto& r = paramRange (id);
    return r.fromNorm (r.toNorm (L.base[(size_t) id]) + o);
}

EnvParams Voice::makeEnv (const LayerRuntime& L, int i) const noexcept
{
    const auto u = (size_t) i;
    EnvParams e;
    e.attack = val (L, lp::envAttack[u]);
    e.hold = val (L, lp::envHold[u]);
    e.decay = val (L, lp::envDecay[u]);
    e.sustain = val (L, lp::envSustain[u]);
    e.release = val (L, lp::envRelease[u]);
    e.attackCurve = idx (L.base[(size_t) lp::envAttackCurve[u]]);
    e.decayCurve = idx (L.base[(size_t) lp::envDecayCurve[u]]);
    e.releaseCurve = idx (L.base[(size_t) lp::envReleaseCurve[u]]);
    e.velocity = val (L, lp::envVelocity[u]);
    e.keyTrack = val (L, lp::envKeyTrack[u]);
    e.loop = on (L.base[(size_t) lp::envLoop[u]]);
    e.retrigger = on (L.base[(size_t) lp::envRetrigger[u]]);
    e.timeScale = val (L, lp::envTimeScale[u]);
    e.amount = val (L, lp::envAmount[u]);
    return e;
}

LfoParams Voice::makeLfo (const LayerRuntime& L, int i) const noexcept
{
    const auto u = (size_t) i;
    LfoParams p;
    p.shape = idx (L.base[(size_t) lp::lfoShape[u]]);
    p.rateHz = val (L, lp::lfoRate[u]);
    p.sync = on (L.base[(size_t) lp::lfoSync[u]]);
    p.division = idx (L.base[(size_t) lp::lfoDivision[u]]);
    p.phase = val (L, lp::lfoPhase[u]) / 360.0f;
    p.offset = val (L, lp::lfoOffset[u]);
    p.delay = val (L, lp::lfoDelay[u]);
    p.fadeIn = val (L, lp::lfoFadeIn[u]);
    p.fadeOut = val (L, lp::lfoFadeOut[u]);
    p.mode = idx (L.base[(size_t) lp::lfoMode[u]]);
    p.smooth = val (L, lp::lfoSmooth[u]);
    p.stereo = val (L, lp::lfoStereoPhase[u]) / 360.0f;
    p.unipolar = on (L.base[(size_t) lp::lfoUnipolar[u]]);
    p.amount = val (L, lp::lfoAmount[u]);
    return p;
}

void Voice::begin (const VoiceStart& s, VoiceGlobals& G)
{
    auto& L = (*G.layers)[(size_t) s.layer];
    state = State::Playing;
    layer = s.layer; channel = s.channel; note = s.note;
    startTime = s.time;
    gate = true; sustained = false; sostenutoLatched = false; pendingRelease = false;
    releaseVelocity = 0.0f;
    velocity = std::pow (std::clamp (s.velocity, 0.0f, 1.0f), std::exp2 (2.0f * L.base[(size_t) lp::velocityCurve]));
    seed = seed * 1664525u + 1013904223u;
    randomValue = (float) (seed >> 8) * (2.0f / 16777216.0f) - 1.0f;

    offsets.fill (0.0f);
    matrix.reset();
    for (int i = 0; i < 4; ++i)
    {
        envP[(size_t) i] = makeEnv (L, i);
        envs[(size_t) i].kill();
        envs[(size_t) i].noteOn (envP[(size_t) i], velocity, note);
        lfoP[(size_t) i] = makeLfo (L, i);
        lfos[(size_t) i].noteOn (lfoP[(size_t) i], L.freePhase[(size_t) i], seed + (uint32_t) i * 7919u);
    }

    glideTarget = (float) note;
    gliding = s.glideFrom >= 0.0f && on (L.base[(size_t) lp::glideOn]);
    glidePitch = gliding ? s.glideFrom : (float) note;
    glideTime = L.base[(size_t) lp::glideTime];
    glideStep = gliding && glideTime > 0.0f ? std::fabs (glideTarget - glidePitch) / glideTime : 0.0f;
    if (glideTime <= 0.0f) { gliding = false; glidePitch = glideTarget; }

    havePitch = false;
    gainL.reset (0.0f); gainR.reset (0.0f);
    currentLevel = 0.0f;
    filterActive = false;   // updateFilter() resets the filter state on the first tick
    filter2Active = false;
    drift.start (seed ^ 0x9E3779B9u);

    if (L.engine != nullptr)
        L.engine->noteOn (index, { note, velocity, channel, true });
}

void Voice::start (const VoiceStart& s, VoiceGlobals& G)
{
    begin (s, G);
    controlTick (G, G.tickRemaining);
}

void Voice::steal (const VoiceStart& s, VoiceGlobals& G)
{
    if (state == State::Playing)
    {
        fadeLayer = layer;
        fadeTotal = fadeRemaining = std::max (1, (int) (0.002 * sr));
        fadeGainL = gainL.current * envs[0].levelRaw() * envs[0].velocityScale();
        fadeGainR = gainR.current * envs[0].levelRaw() * envs[0].velocityScale();
        ep.pitchStart = ep.pitchEnd = pitchEnd;
    }
    else if (state == State::Free)
    {
        start (s, G);
        return;
    }
    // Logical identity changes immediately; audio switches after the fade.
    pending = s;
    state = State::Stealing;
    layer = s.layer; channel = s.channel; note = s.note; startTime = s.time;
    gate = true; sustained = false; sostenutoLatched = false; pendingRelease = false;
}

void Voice::legato (int newNote, float vel, bool retrigger, bool glide, VoiceGlobals& G)
{
    if (state == State::Stealing)
    {
        pending.note = newNote;
        note = newNote;
        return;
    }
    auto& L = (*G.layers)[(size_t) layer];
    note = newNote;
    gate = true; sustained = false; pendingRelease = false;
    if (retrigger)
    {
        velocity = std::pow (std::clamp (vel, 0.0f, 1.0f), std::exp2 (2.0f * L.base[(size_t) lp::velocityCurve]));
        for (int i = 0; i < 4; ++i)
        {
            envs[(size_t) i].noteOn (envP[(size_t) i], velocity, note);
            if (lfoP[(size_t) i].mode != 0)
                lfos[(size_t) i].noteOn (lfoP[(size_t) i], L.freePhase[(size_t) i], seed + (uint32_t) i * 7919u);
        }
    }
    glideTarget = (float) newNote;
    glideTime = L.base[(size_t) lp::glideTime];
    gliding = glide && glideTime > 0.0f;
    if (gliding)
        glideStep = std::fabs (glideTarget - glidePitch) / glideTime;
    else
        glidePitch = glideTarget;
    if (L.engine != nullptr)
        L.engine->noteOn (index, { note, velocity, channel, retrigger });
}

void Voice::release (float relVel, VoiceGlobals& G)
{
    if (state == State::Stealing) { pendingRelease = true; releaseVelocity = relVel; return; }
    if (state != State::Playing) return;
    releaseVelocity = relVel;
    gate = false; sustained = false;
    for (int i = 0; i < 4; ++i)
    {
        envs[(size_t) i].noteOff (envP[(size_t) i]);
        lfos[(size_t) i].noteOff();
    }
    if (auto* e = (*G.layers)[(size_t) layer].engine)
        e->noteOff (index);
}

void Voice::freeVoice (VoiceGlobals& G)
{
    if (auto* e = (*G.layers)[(size_t) layer].engine)
        e->kill (index);
    state = State::Free;
    gate = sustained = sostenutoLatched = false;
    currentLevel = 0.0f;
}

void Voice::kill (VoiceGlobals& G)
{
    if (state == State::Stealing)
        if (auto* e = (*G.layers)[(size_t) fadeLayer].engine)
            e->kill (index);
    if (state != State::Free)
        freeVoice (G);
}

void Voice::controlTick (VoiceGlobals& G, int n) noexcept
{
    if (state != State::Playing || n <= 0)
        return;

    auto& L = (*G.layers)[(size_t) layer];
    const int g = G.mpe.globalChannel (channel);
    const bool member = G.mpe.isMember (channel);
    const auto& gc = G.midi->channels[(size_t) g];
    const auto& vc = G.midi->channels[(size_t) channel];
    const float atSens = L.base[(size_t) lp::aftertouchSens];
    const float dt = (float) n / (float) sr;

    ModSourceValues src;
    src.cc = &gc.cc;
    auto& v = src.v;
    using S = ModSource;
    for (int i = 0; i < 4; ++i)
    {
        v[(size_t) S::Lfo1 + (size_t) i] = lfos[(size_t) i].value();
        v[(size_t) S::AmpEnv + (size_t) i] = envs[(size_t) i].output (envP[(size_t) i]);
    }
    const float pressure = member ? vc.pressure : vc.polyPressure[(size_t) note];
    v[(size_t) S::Velocity] = velocity;
    v[(size_t) S::ReleaseVelocity] = releaseVelocity;
    v[(size_t) S::Aftertouch] = gc.pressure * atSens;
    v[(size_t) S::MpePressure] = pressure * atSens;
    v[(size_t) S::MpeSlide] = vc.cc[74];
    v[(size_t) S::MpePitch] = member ? vc.bend : 0.0f;
    v[(size_t) S::ModWheel] = gc.cc[1] * L.base[(size_t) lp::modwheelAmount];
    v[(size_t) S::PitchWheel] = gc.bend;
    v[(size_t) S::KeyTrack] = (float) (note - 60) / 60.0f;
    v[(size_t) S::NoteNumber] = (float) note / 127.0f;
    v[(size_t) S::Random] = randomValue;
    v[(size_t) S::Breath] = gc.cc[2];
    v[(size_t) S::Expression] = gc.cc[11];
    for (size_t m = 0; m < 8; ++m)
        v[(size_t) S::Macro1 + m] = G.macros[m];

    matrix.process (L.slots, src, dt, offsets.data());

    for (int i = 0; i < 4; ++i)
    {
        envP[(size_t) i] = makeEnv (L, i);
        if (i > 0) envs[(size_t) i].advance (envP[(size_t) i], n);
        lfoP[(size_t) i] = makeLfo (L, i);
        lfos[(size_t) i].tick (lfoP[(size_t) i], dt, G.bpm, L.custom[(size_t) i]);
    }

    // Glide
    if (gliding)
    {
        if (idx (L.base[(size_t) lp::glideCurve]) == 0)
        {
            const float step = glideStep * dt;
            if (std::fabs (glideTarget - glidePitch) <= step) { glidePitch = glideTarget; gliding = false; }
            else glidePitch += glidePitch < glideTarget ? step : -step;
        }
        else
        {
            const float c = std::exp (-dt * 5.0f / std::max (glideTime, 1.0e-3f));
            glidePitch = glideTarget + (glidePitch - glideTarget) * c;
            if (std::fabs (glidePitch - glideTarget) < 1.0e-3f) { glidePitch = glideTarget; gliding = false; }
        }
    }

    const float noteBend = member ? vc.bend * (float) G.mpe.bendRange : 0.0f;
    const float bend = gc.bend * L.base[(size_t) lp::bendRange] + noteBend;
    const float target = glidePitch + L.transpose + val (L, lp::pitch) + bend;
    pitchStart = havePitch ? pitchEnd : target;
    pitchEnd = target;
    havePitch = true;

    float level = val (L, lp::level);
    if (on (L.base[(size_t) lp::expressionOn]))
        level *= gc.cc[11];
    const float pan = std::clamp (val (L, lp::panMod), -1.0f, 1.0f);
    const float angle = (pan + 1.0f) * 0.25f * 3.14159265f;
    const float tl = std::cos (angle) * 1.41421356f * level, tr = std::sin (angle) * 1.41421356f * level;
    if (tickLen == 0 || (gainL.current == 0.0f && gainR.current == 0.0f && currentLevel == 0.0f))
    {
        gainL.reset (tl); gainR.reset (tr);
    }
    else
    {
        gainL.setTarget (tl, n); gainR.setTarget (tr, n);
    }

    ep.unison = idx (val (L, lp::unison));
    ep.detuneCents = val (L, lp::unisonDetune);
    ep.spread = val (L, lp::unisonSpread);
    ep.unisonPan = val (L, lp::unisonPan);
    ep.base = L.base.data();
    ep.offsets = offsets.data();
    for (size_t i = 0; i < 4; ++i)
        lfoOut[i] = lfos[i].value();
    ep.lfo = lfoOut.data();
    updateFilter (L, dt);

    if (L.engine != nullptr)
        L.engine->setNoteExpression (index, { noteBend, pressure * atSens, vc.cc[74] });

    tickLen = n;
    tickPos = 0;
}

void Voice::render (VoiceGlobals& G, int start, int n) noexcept
{
    int consumed = 0;
    while (n > 0)
    {
        if (state == State::Free)
            return;

        if (state == State::Stealing)
        {
            const int m = std::min (n, fadeRemaining);
            if (auto* e = (*G.layers)[(size_t) fadeLayer].engine; e != nullptr && m > 0)
            {
                renderEngine (*e, ep, G, m, 1.0f, 1.0f);
                float* outL = G.layerL[(size_t) fadeLayer] + start;
                float* outR = G.layerR[(size_t) fadeLayer] + start;
                for (int i = 0; i < m; ++i)
                {
                    const float f = (float) (fadeRemaining - i) / (float) fadeTotal;
                    outL[i] += G.scratchL[i] * fadeGainL * f;
                    outR[i] += G.scratchR[i] * fadeGainR * f;
                }
            }
            fadeRemaining -= m;
            start += m; n -= m; consumed += m;
            if (fadeRemaining <= 0)
            {
                if (auto* e = (*G.layers)[(size_t) fadeLayer].engine)
                    e->kill (index);
                const bool rel = pendingRelease;
                const float rv = releaseVelocity;
                begin (pending, G);
                controlTick (G, std::max (1, G.tickRemaining - consumed));
                if (rel) release (rv, G);
            }
            continue;
        }

        auto& L = (*G.layers)[(size_t) layer];
        if (L.engine == nullptr) { freeVoice (G); return; }

        const float inv = 1.0f / (float) std::max (1, tickLen);
        const float p0 = (float) tickPos * inv, p1 = (float) std::min (tickPos + n, tickLen) * inv;
        EngineVoiceParams rp = ep;
        rp.pitchStart = pitchStart + (pitchEnd - pitchStart) * p0;
        rp.pitchEnd = pitchStart + (pitchEnd - pitchStart) * p1;
        renderEngine (*L.engine, rp, G, n, p0, p1);

        float* outL = G.layerL[(size_t) layer] + start;
        float* outR = G.layerR[(size_t) layer] + start;
        auto& amp = envs[0];
        const auto& ap = envP[0];
        const float ampScale = amp.velocityScale() * std::fabs (ap.amount);
        float a = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            a = amp.next (ap) * ampScale;
            const float gl = gainL.next() * a, gr = gainR.next() * a;
            outL[i] += G.scratchL[i] * gl;
            outR[i] += G.scratchR[i] * gr;
        }
        currentLevel = a * 0.5f * (gainL.current + gainR.current);
        tickPos += n;
        if (! amp.isActive())
            freeVoice (G);
        return;
    }
}
void Voice::updateFilter (const LayerRuntime& L, float dt) noexcept
{
    driftActive = on (L.base[(size_t) lp::driftOn]);
    if (driftActive)
        drift.tick (val (L, lp::driftAmount), dt);
    ep.driftSemis = driftActive ? drift.pitchSemis() : nullptr;

    const bool bypassAll = driftActive && on (L.base[(size_t) lp::driftOnly]);
    dual = L.engine != nullptr && L.engine->dualFilter();
    updateFilter2 (L, bypassAll);
    const bool want = on (L.base[(size_t) lp::filterOn]) && ! bypassAll;
    if (! want) { filterActive = false; return; }
    const auto model = (FilterModel) std::clamp (idx (L.base[(size_t) lp::filterModel]), 0, (int) FilterModel::count - 1);
    if (! filterActive)
    {
        filter.setModel (model);
        filter.reset();
        haveCutoff = false;
        filterActive = true;
    }
    filter.setModel (model);
    filter.setResonance (std::clamp (val (L, lp::filterResonance) + (driftActive ? drift.resonanceOffset() : 0.0f), 0.0f, 1.0f));
    filter.setDrive (val (L, lp::filterDrive));

    float oct = std::log2 (std::max (val (L, lp::filterCutoff), 1.0f));
    oct += val (L, lp::filterKeytrack) * (pitchEnd - 60.0f) / 12.0f;
    oct += val (L, lp::filterEnvAmount) * 10.0f * envs[1].output (envP[1]);
    oct += val (L, lp::filterVelocity) * (velocity - 1.0f) * 4.0f;
    oct += val (L, lp::filterLfoAmount) * 4.0f * lfos[0].value();
    if (driftActive) oct += drift.cutoffOctaves();
    const float hz = std::clamp (std::exp2 (oct), 20.0f, 20000.0f);
    cutStart = haveCutoff ? cutEnd : hz;
    cutEnd = hz;
    haveCutoff = true;
}

void Voice::renderEngine (IEngine& e, EngineVoiceParams p, VoiceGlobals& G, int n, float p0, float p1) noexcept
{
    p.bypassL = G.bypassL;
    p.bypassR = G.bypassR;
    const bool bypass = e.render (index, p, G.scratchL, G.scratchR, n) && G.bypassL != nullptr;
    runFilter (G.scratchL, G.scratchR, n, p0, p1);
    if (bypass)
        for (int i = 0; i < n; ++i)
        {
            G.scratchL[i] += G.bypassL[i];
            G.scratchR[i] += G.bypassR[i];
        }
}

void Voice::updateFilter2 (const LayerRuntime& L, bool bypassAll) noexcept
{
    // Filter 2 + Serial/Parallel routing (§3D, engines with dualFilter()).
    if (! dual || bypassAll || ! on (L.base[(size_t) lp::wtF2On])) { filter2Active = false; return; }
    const auto model = (FilterModel) std::clamp (idx (L.base[(size_t) lp::wtF2Model]), 0, (int) FilterModel::count - 1);
    if (! filter2Active)
    {
        filter2.setModel (model);
        filter2.reset();
        haveCutoff2 = false;
        filter2Active = true;
    }
    filter2.setModel (model);
    filter2.setResonance (std::clamp (val (L, lp::wtF2Resonance) + (driftActive ? drift.resonanceOffset() : 0.0f), 0.0f, 1.0f));
    filter2.setDrive (val (L, lp::wtF2Drive));
    float oct = std::log2 (std::max (val (L, lp::wtF2Cutoff), 1.0f));
    oct += val (L, lp::wtF2Keytrack) * (pitchEnd - 60.0f) / 12.0f;
    oct += val (L, lp::wtF2EnvAmount) * 10.0f * envs[2].output (envP[2]);
    oct += val (L, lp::wtF2Velocity) * (velocity - 1.0f) * 4.0f;
    oct += val (L, lp::wtF2LfoAmount) * 4.0f * lfos[1].value();
    if (driftActive) oct += drift.cutoffOctaves();
    const float hz = std::clamp (std::exp2 (oct), 20.0f, 20000.0f);
    cut2Start = haveCutoff2 ? cut2End : hz;
    cut2End = hz;
    haveCutoff2 = true;
    routing = idx (L.base[(size_t) lp::wtFilterRouting]);
    fMix = std::clamp (val (L, lp::wtFilterMix), 0.0f, 1.0f);
    fPan = std::clamp (val (L, lp::wtFilterPan), -1.0f, 1.0f);
    fStereo = std::clamp (val (L, lp::wtFilterStereo), 0.0f, 2.0f);
}

void Voice::runFilter (float* l, float* r, int n, float p0, float p1) noexcept
{
    if (n <= 0)
        return;
    const bool f2 = dual && filter2Active;
    if (! f2)
    {
        if (! filterActive)
            return;
        const float ratio = cutEnd / cutStart;
        filter.process (l, r, n, cutStart * std::pow (ratio, p0), cutStart * std::pow (ratio, p1));
        return;
    }
    // chunks of controlBlock (scratch size)
    for (int done = 0; done < n;)
    {
        const int m = std::min (controlBlock, n - done);
        const float q0 = p0 + (p1 - p0) * (float) done / (float) n, q1 = p0 + (p1 - p0) * (float) (done + m) / (float) n;
        float* L = l + done;
        float* R = r + done;
        float* tL = tmpL.data();
        float* tR = tmpR.data();
        const auto run1 = [&] (float* a, float* b) noexcept
        {
            if (! filterActive) return;
            const float ratio = cutEnd / cutStart;
            filter.process (a, b, m, cutStart * std::pow (ratio, q0), cutStart * std::pow (ratio, q1));
        };
        const auto run2 = [&] (float* a, float* b) noexcept
        {
            const float ratio = cut2End / cut2Start;
            filter2.process (a, b, m, cut2Start * std::pow (ratio, q0), cut2Start * std::pow (ratio, q1));
        };
        if (routing == 0)   // Serial: F1 -> F2, Mix = dry/wet of F2
        {
            run1 (L, R);
            std::copy (L, L + m, tL);
            std::copy (R, R + m, tR);
            run2 (tL, tR);
            for (int i = 0; i < m; ++i)
            {
                L[i] += fMix * (tL[i] - L[i]);
                R[i] += fMix * (tR[i] - R[i]);
            }
        }
        else                // Parallel: equal-power balance, Pan spreads F1 / F2
        {
            std::copy (L, L + m, tL);
            std::copy (R, R + m, tR);
            run1 (L, R);
            run2 (tL, tR);
            const float g1 = std::cos (fMix * 1.5707963f), g2 = std::sin (fMix * 1.5707963f);
            const float aL = g1 * std::min (1.0f, 1.0f + fPan), aR = g1 * std::min (1.0f, 1.0f - fPan);
            const float bL = g2 * std::min (1.0f, 1.0f - fPan), bR = g2 * std::min (1.0f, 1.0f + fPan);
            for (int i = 0; i < m; ++i)
            {
                L[i] = aL * L[i] + bL * tL[i];
                R[i] = aR * R[i] + bR * tR[i];
            }
        }
        if (fStereo != 1.0f)
            for (int i = 0; i < m; ++i)
            {
                const float mid = 0.5f * (L[i] + R[i]), side = 0.5f * (L[i] - R[i]) * fStereo;
                L[i] = mid + side;
                R[i] = mid - side;
            }
        done += m;
    }
}

} // namespace chroma
