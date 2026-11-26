#include "Synth.h"
#include "engines/analog/AnalogEngine.h"
#include "engines/additive/AdditiveEngine.h"
#include "engines/fm/FmEngine.h"
#include "engines/pd/PdEngine.h"
#include "engines/wavetable/WavetableEngine.h"
#include "shared/ParamRange.h"
#include "shared/Scale.h"

namespace chroma
{
namespace
{
inline bool on (float v) noexcept { return v > 0.5f; }
inline int idx (float v) noexcept { return (int) std::lround (v); }

const std::array<const std::array<const char*, numLayers>*, 13> layerIdArrays {
    &ParamIDs::layerOn, &ParamIDs::layerEngine, &ParamIDs::layerVolume, &ParamIDs::layerPan, &ParamIDs::layerMute,
    &ParamIDs::layerSolo, &ParamIDs::layerTranspose, &ParamIDs::layerKeyLo, &ParamIDs::layerKeyHi,
    &ParamIDs::layerVelLo, &ParamIDs::layerVelHi, &ParamIDs::layerMidiChannel, &ParamIDs::layerMaxVoices };

// Master FX host parameters, in the order of params/master_fx.yaml.
enum MasterFxId { driveOn, driveType, driveDrive, driveTone, driveMix, driveOutput, chorusOn, chorusMode, chorusRate, chorusDepth, chorusMix, delayOn, delayType, delayTimeL, delayTimeR, delaySync, delayDivL, delayDivR, delayFeedback, delayHp, delayLp, delayWidth, delayMod, delayDucking, delayMix, reverbOn, reverbType, reverbSize, reverbDecay, reverbPredelay, reverbDamping, reverbDiffusion, reverbMod, reverbLowcut, reverbHighcut, reverbWidth, reverbMix, filterOn, hpOn, hpFreq, hpRes, bpOn, bpFreq, bpWidth, bpRes, lpOn, lpFreq, lpRes, numMasterFxParams };
const std::array<const char*, numMasterFxParams> masterFxIds { ParamIDs::mfxDriveOn, ParamIDs::mfxDriveType, ParamIDs::mfxDriveDrive, ParamIDs::mfxDriveTone, ParamIDs::mfxDriveMix, ParamIDs::mfxDriveOutput, ParamIDs::mfxChorusOn, ParamIDs::mfxChorusMode, ParamIDs::mfxChorusRate, ParamIDs::mfxChorusDepth, ParamIDs::mfxChorusMix, ParamIDs::mfxDelayOn, ParamIDs::mfxDelayType, ParamIDs::mfxDelayTimeL, ParamIDs::mfxDelayTimeR, ParamIDs::mfxDelaySync, ParamIDs::mfxDelayDivL, ParamIDs::mfxDelayDivR, ParamIDs::mfxDelayFeedback, ParamIDs::mfxDelayHp, ParamIDs::mfxDelayLp, ParamIDs::mfxDelayWidth, ParamIDs::mfxDelayMod, ParamIDs::mfxDelayDucking, ParamIDs::mfxDelayMix, ParamIDs::mfxReverbOn, ParamIDs::mfxReverbType, ParamIDs::mfxReverbSize, ParamIDs::mfxReverbDecay, ParamIDs::mfxReverbPredelay, ParamIDs::mfxReverbDamping, ParamIDs::mfxReverbDiffusion, ParamIDs::mfxReverbMod, ParamIDs::mfxReverbLowcut, ParamIDs::mfxReverbHighcut, ParamIDs::mfxReverbWidth, ParamIDs::mfxReverbMix, ParamIDs::mfxFilterOn, ParamIDs::mfxHpOn, ParamIDs::mfxHpFreq, ParamIDs::mfxHpRes, ParamIDs::mfxBpOn, ParamIDs::mfxBpFreq, ParamIDs::mfxBpWidth, ParamIDs::mfxBpRes, ParamIDs::mfxLpOn, ParamIDs::mfxLpFreq, ParamIDs::mfxLpRes };

juce::String layerSuffix (const char* id) { return juce::String (id).fromFirstOccurrenceOf ("_", false, false); }
} // namespace

Synth::Synth (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& s) : processor (p), apvts (s)
{
    for (auto* prm : processor.getParameters())
        hostParams.push_back (dynamic_cast<juce::RangedAudioParameter*> (prm));
    hostOverride.assign (hostParams.size(), -1.0f);

    auto get = [this] (const char* id) { auto* r = apvts.getParameter (id); jassert (r != nullptr); return r; };
    masterVolume = get (ParamIDs::masterVolume);
    polyphony = get (ParamIDs::masterPolyphony);
    scaleOn = get (ParamIDs::scaleOn);
    mpeOn = get (ParamIDs::mpeOn);
    mpeZone = get (ParamIDs::mpeZone);
    mpeBend = get (ParamIDs::mpeBendRange);
    for (size_t i = 0; i < 12; ++i) scaleSlots[i] = get (ParamIDs::scaleSlot[i]);
    for (size_t i = 0; i < numMacros; ++i) macros[i] = get (ParamIDs::macro[i]);
    for (size_t i = 0; i < numAutomationSlots; ++i) autoSlots[i] = get (ParamIDs::automationSlot[i]);
    for (auto* id : masterFxIds) masterFxParams.push_back (get (id));

    for (size_t l = 0; l < numLayers; ++l)
    {
        auto& h = hostLayers[l];
        h = { get (ParamIDs::layerOn[l]), get (ParamIDs::layerEngine[l]), get (ParamIDs::layerVolume[l]),
              get (ParamIDs::layerPan[l]), get (ParamIDs::layerMute[l]), get (ParamIDs::layerSolo[l]),
              get (ParamIDs::layerTranspose[l]), get (ParamIDs::layerKeyLo[l]), get (ParamIDs::layerKeyHi[l]),
              get (ParamIDs::layerVelLo[l]), get (ParamIDs::layerVelHi[l]), get (ParamIDs::layerMidiChannel[l]),
              get (ParamIDs::layerMaxVoices[l]) };
        // Analog (§3A), Additive (§3B), FM6 (§3C), Wavetable (§3D), Phase Distortion (§3E).
        analogEngines[l] = std::make_unique<AnalogEngine>();
        {
            auto a = std::make_unique<AdditiveEngine>();
            a->setCustomSpectra (&stores[l].additiveSpectrum (0), &stores[l].additiveSpectrum (1));
            addEngines[l] = std::move (a);
        }
        fmEngines[l] = std::make_unique<FmEngine>();
        {
            auto w = std::make_unique<WavetableEngine>();
            w->setUserSlots (stores[l].userTableSlots());
            wtEngines[l] = std::move (w);
        }
        {
            auto d = std::make_unique<PdEngine>();
            d->setCustomCurve (&stores[l].pdCurve());
            pdEngines[l] = std::move (d);
        }
        engineIndex[l] = 0;
        runtime[l].engine = analogEngines[l].get();
        for (size_t i = 0; i < 4; ++i)
            runtime[l].custom[i] = &stores[l].customTable ((int) i);
        for (int i = 0; i < lp::count; ++i)
            runtime[l].base[(size_t) i] = stores[l].get (i);
    }

    globals.midi = &midi;
    globals.layers = &runtime;
}

Synth::~Synth() = default;

void Synth::prepare (double sr, int maxBlockSize, int numOutputChannels)
{
    sampleRate = sr;
    for (size_t l = 0; l < numLayers; ++l)
    {
        analogEngines[l]->prepare (sr, controlBlock, maxPoolVoices);
        fmEngines[l]->prepare (sr, controlBlock, maxPoolVoices);
        addEngines[l]->prepare (sr, controlBlock, maxPoolVoices);
        wtEngines[l]->prepare (sr, controlBlock, maxPoolVoices);
        pdEngines[l]->prepare (sr, controlBlock, maxPoolVoices);
        layers[l].bufL.assign (controlBlock, 0.0f);
        layers[l].bufR.assign (controlBlock, 0.0f);
        layers[l].gainL.reset (0.0f);
        layers[l].gainR.reset (0.0f);
        layers[l].stackSize = 0;
        layers[l].monoVoice = -1;
        globals.layerL[l] = layers[l].bufL.data();
        globals.layerR[l] = layers[l].bufR.data();
    }
    scratchL.assign (controlBlock, 0.0f);
    scratchR.assign (controlBlock, 0.0f);
    globals.scratchL = scratchL.data();
    globals.scratchR = scratchR.data();
    bypassL.assign (controlBlock, 0.0f);
    bypassR.assign (controlBlock, 0.0f);
    globals.bypassL = bypassL.data();
    globals.bypassR = bypassR.data();
    globals.sampleRate = sr;
    for (int i = 0; i < maxPoolVoices; ++i)
        voices[(size_t) i].prepare (i, sr);
    midi.reset();
    paramSmoother.setTime (0.02f, sr);
    masterGain.reset (juce::Decibels::decibelsToGain (masterVolume->convertFrom0to1 (masterVolume->getValue()), -60.0f));
    limiter.prepare (sr, numOutputChannels);
    for (auto& f : layerFx) f.prepare (sr);
    masterFx.prepare (sr);
    monoScratch.assign ((size_t) juce::jmax (maxBlockSize, 512), 0.0f);
    tickRemaining = 0;
    firstBlock = true;
}

float Synth::hostValue (const juce::RangedAudioParameter* p) const noexcept
{
    const auto i = (size_t) p->getParameterIndex();
    const float ov = i < hostOverride.size() ? hostOverride[i] : -1.0f;
    return p->convertFrom0to1 (ov >= 0.0f ? ov : p->getValue());
}

void Synth::readHostParams() noexcept
{
    std::fill (hostOverride.begin(), hostOverride.end(), -1.0f);
    numLayerOverrides = 0;

    for (size_t m = 0; m < numMacros; ++m)
        globals.macros[m] = macros[m]->getValue();

    const auto& am = automationMap.read();
    for (size_t s = 0; s < numAutomationSlots; ++s)
    {
        const auto& t = am.slots[s];
        if (t.layer >= 0 && t.layer < numLayers && t.id >= 0 && t.id < lp::count)
            layerOverrides[(size_t) numLayerOverrides++] = { t.layer, t.id, paramRange (t.id).fromNorm (autoSlots[s]->getValue()) };
    }
    const auto& mm = macroMap.read();
    for (int i = 0; i < mm.count; ++i)
    {
        const auto& a = mm.items[(size_t) i];
        const float norm = a.min + (a.max - a.min) * globals.macros[(size_t) juce::jlimit (0, numMacros - 1, a.macro)];
        if (a.target.layer == ParamTarget::host && a.target.id >= 0 && a.target.id < (int) hostOverride.size())
            hostOverride[(size_t) a.target.id] = juce::jlimit (0.0f, 1.0f, norm);
        else if (a.target.layer >= 0 && a.target.layer < numLayers && a.target.id >= 0 && a.target.id < lp::count
                 && numLayerOverrides < (int) layerOverrides.size())
            layerOverrides[(size_t) numLayerOverrides++] = { a.target.layer, a.target.id, paramRange (a.target.id).fromNorm (norm) };
    }

    static constexpr int poolSizes[] { 12, 24, 36 };
    pool = poolSizes[juce::jlimit (0, 2, idx (hostValue (polyphony)))];
    scaleEnabled = on (hostValue (scaleOn));
    for (size_t i = 0; i < 12; ++i)
        scaleMasks[i] = (uint16_t) juce::jlimit (0, 4095, idx (hostValue (scaleSlots[i])));
    globals.mpe.enabled = on (hostValue (mpeOn));
    globals.mpe.upperZone = idx (hostValue (mpeZone)) == 1;
    globals.mpe.bendRange = idx (hostValue (mpeBend));

    anySolo = false;
    for (size_t l = 0; l < numLayers; ++l)
        anySolo = anySolo || (on (hostValue (hostLayers[l].on)) && on (hostValue (hostLayers[l].solo)));

    for (size_t l = 0; l < numLayers; ++l)
    {
        const auto& h = hostLayers[l];
        auto& st = layers[l];
        st.on = on (hostValue (h.on));
        st.audible = st.on && ! on (hostValue (h.mute)) && (! anySolo || on (hostValue (h.solo)));
        const float db = hostValue (h.volume);
        st.volumeGain = db <= -59.99f ? 0.0f : juce::Decibels::decibelsToGain (db);
        st.pan = hostValue (h.pan);
        runtime[l].transpose = (float) idx (hostValue (h.transpose));
        st.keyLo = idx (hostValue (h.keyLo)); st.keyHi = idx (hostValue (h.keyHi));
        st.velLo = idx (hostValue (h.velLo)); st.velHi = idx (hostValue (h.velHi));
        st.midiCh = idx (hostValue (h.midiCh));
        st.maxVoices = idx (hostValue (h.maxVoices));

        // Engine change: free the layer's voices, then switch (all engines are preallocated).
        const int e = juce::jlimit (0, 4, idx (hostValue (h.engine)));
        if (e != engineIndex[l])
        {
            for (auto& v : voices)
                if (v.state != Voice::State::Free && (v.layer == (int) l))
                    v.kill (globals);
            st.stackSize = 0;
            st.monoVoice = -1;
            engineIndex[l] = e;
            runtime[l].engine = e == 0 ? analogEngines[l].get() : e == 1 ? addEngines[l].get() : e == 2 ? fmEngines[l].get()
                             : e == 3 ? wtEngines[l].get() : pdEngines[l].get();
        }
    }
}

int Synth::layerVoiceCount (int layer) const noexcept
{
    int c = 0;
    for (const auto& v : voices)
        if (v.state != Voice::State::Free && v.layer == layer) ++c;
    return c;
}

bool Synth::layerMono (int layer) const noexcept
{
    return idx (runtime[(size_t) layer].base[(size_t) lp::voiceMode]) != 1;
}

void Synth::updateLayerBases (int samples) noexcept
{
    std::array<int, numLayers> counts {};
    for (const auto& v : voices)
        if (v.state != Voice::State::Free) ++counts[(size_t) v.layer];

    const float c = std::pow (paramSmoother.coeffPerSample, (float) samples);
    const float dt = (float) samples / (float) sampleRate;
    std::array<float, lp::count> target;

    for (size_t l = 0; l < numLayers; ++l)
    {
        auto& st = layers[l];
        const bool active = st.on || counts[l] > 0 || st.gainL.current > 0.0f || st.gainR.current > 0.0f;
        const bool justActivated = active && ! st.active;
        st.active = active;
        if (! active)
            continue;

        auto& rt = runtime[l];
        for (int i = 0; i < lp::count; ++i)
            target[(size_t) i] = stores[l].get (i);
        for (int i = 0; i < numLayerOverrides; ++i)
            if (layerOverrides[(size_t) i].layer == (int) l)
                target[(size_t) layerOverrides[(size_t) i].id] = layerOverrides[(size_t) i].plain;

        for (int i = 0; i < lp::count; ++i)
        {
            auto& b = rt.base[(size_t) i];
            const float t = target[(size_t) i];
            b = (lp::descs[(size_t) i].type == lp::Type::Float && ! justActivated && ! firstBlock) ? t + (b - t) * c : t;
        }

        for (size_t s = 0; s < (size_t) modSlots; ++s)
        {
            auto& slot = rt.slots[s];
            slot.enabled = on (rt.base[(size_t) lp::mmEnable[s]]);
            slot.source = idx (rt.base[(size_t) lp::mmSource[s]]);
            slot.cc = idx (rt.base[(size_t) lp::mmCC[s]]);
            const int d = idx (rt.base[(size_t) lp::mmDest[s]]);
            slot.dest = d > 0 && d <= (int) lp::modDestinations.size() ? (int) lp::modDestinations[(size_t) d - 1] : -1;
            slot.amount = rt.base[(size_t) lp::mmAmount[s]];
            slot.via = idx (rt.base[(size_t) lp::mmVia[s]]);
            slot.curve = idx (rt.base[(size_t) lp::mmCurve[s]]);
            slot.smooth = rt.base[(size_t) lp::mmSmooth[s]];
            slot.invert = on (rt.base[(size_t) lp::mmInvert[s]]);
        }

        for (size_t i = 0; i < 4; ++i)
        {
            LfoParams p;
            p.rateHz = rt.base[(size_t) lp::lfoRate[i]];
            p.sync = on (rt.base[(size_t) lp::lfoSync[i]]);
            p.division = idx (rt.base[(size_t) lp::lfoDivision[i]]);
            rt.freePhase[i] += (double) lfoCyclesPerSecond (p, globals.bpm) * dt;
            rt.freePhase[i] -= std::floor (rt.freePhase[i]);
        }
    }
    firstBlock = false;
}

void Synth::controlTick() noexcept
{
    updateLayerBases (controlBlock);

    for (size_t l = 0; l < numLayers; ++l)
    {
        auto& st = layers[l];
        const float g = st.audible ? st.volumeGain : 0.0f;
        const float gl = g * (st.pan > 0.0f ? 1.0f - st.pan : 1.0f);
        const float gr = g * (st.pan < 0.0f ? 1.0f + st.pan : 1.0f);
        st.gainL.setTarget (gl, controlBlock);
        st.gainR.setTarget (gr, controlBlock);

        // Layer switched off: once silent, free its voices (zero CPU).
        if (! st.on && st.gainL.current == 0.0f && st.gainR.current == 0.0f)
        {
            for (auto& v : voices)
                if (v.state != Voice::State::Free && v.layer == (int) l)
                    v.kill (globals);
            st.stackSize = 0;
            st.monoVoice = -1;
        }
    }

    for (auto& v : voices)
        if (v.state == Voice::State::Playing)
            v.controlTick (globals, controlBlock);

    updateLayerFx();
}

// Layer FX parameters: layer base values + the mod matrix offsets of the layer's newest voice
// (the FX are one insert per layer, so per-voice modulation follows the last played note).
void Synth::updateLayerFx() noexcept
{
    for (size_t l = 0; l < numLayers; ++l)
    {
        if (! layers[l].active) continue;
        const auto& rt = runtime[l];
        const float* off = nullptr;
        uint64_t newest = 0;
        for (const auto& v : voices)
            if (v.state == Voice::State::Playing && v.layer == (int) l && (off == nullptr || v.startTime >= newest))
            {
                off = v.modOffsets();
                newest = v.startTime;
            }
        auto val = [&] (int id)
        {
            const float b = rt.base[(size_t) id];
            if (off == nullptr || off[id] == 0.0f) return b;
            const auto& r = paramRange (id);
            return r.fromNorm (r.toNorm (b) + off[id]);
        };
        auto sw = [&] (int id) { return on (rt.base[(size_t) id]); };
        auto ch = [&] (int id) { return idx (rt.base[(size_t) id]); };

        fx::LayerFxParams p;
        p.distOn = sw (lp::fxDistOn);
        p.dist = { fx::layerDistType (ch (lp::fxDistType)), val (lp::fxDistDrive), val (lp::fxDistTone), val (lp::fxDistMix), val (lp::fxDistOutput) };
        p.chorusOn = sw (lp::fxChorusOn);
        p.chorus = { fx::ChorusMode::Custom, val (lp::fxChorusRate), val (lp::fxChorusDepth), val (lp::fxChorusDelay),
                     val (lp::fxChorusFeedback), val (lp::fxChorusWidth), val (lp::fxChorusMix) };
        p.phaserOn = sw (lp::fxPhaserOn);
        p.phaser = { val (lp::fxPhaserRate), val (lp::fxPhaserDepth), val (lp::fxPhaserFeedback), val (lp::fxPhaserFreq),
                     val (lp::fxPhaserMix), ch (lp::fxPhaserStages) };
        p.flangerOn = sw (lp::fxFlangerOn);
        p.flanger = { val (lp::fxFlangerRate), val (lp::fxFlangerDepth), val (lp::fxFlangerFeedback), val (lp::fxFlangerDelay),
                      val (lp::fxFlangerStereo), val (lp::fxFlangerMix) };
        p.delayOn = sw (lp::fxDelayOn);
        const bool sync = sw (lp::fxDelaySync);
        p.delay.type = fx::DelayType::Digital;
        p.delay.timeL = sync ? fx::divisionSeconds (ch (lp::fxDelayDivL), globals.bpm) : val (lp::fxDelayTimeL) * 0.001f;
        p.delay.timeR = sync ? fx::divisionSeconds (ch (lp::fxDelayDivR), globals.bpm) : val (lp::fxDelayTimeR) * 0.001f;
        p.delay.feedback = val (lp::fxDelayFeedback);
        p.delay.hpHz = val (lp::fxDelayHp);
        p.delay.lpHz = val (lp::fxDelayLp);
        p.delay.width = val (lp::fxDelayWidth);
        p.delay.mix = val (lp::fxDelayMix);
        p.delayPingPong = sw (lp::fxDelayPingpong);
        p.reverbOn = sw (lp::fxReverbOn);
        p.reverb = { fx::ReverbType::Hall, val (lp::fxReverbSize), val (lp::fxReverbDecay), val (lp::fxReverbPredelay),
                     val (lp::fxReverbDamping), val (lp::fxReverbDiffusion), 0.2f, val (lp::fxReverbLowcut),
                     val (lp::fxReverbHighcut), val (lp::fxReverbWidth), val (lp::fxReverbMix) };
        p.eqOn = sw (lp::fxEqOn);
        p.eq = { val (lp::fxEqLowFreq), val (lp::fxEqLowGain), val (lp::fxEqLowQ), val (lp::fxEqMidFreq), val (lp::fxEqMidGain),
                 val (lp::fxEqMidQ), val (lp::fxEqHighFreq), val (lp::fxEqHighGain), val (lp::fxEqHighQ) };
        p.compOn = sw (lp::fxCompOn);
        p.comp = { val (lp::fxCompThreshold), val (lp::fxCompRatio), val (lp::fxCompAttack), val (lp::fxCompRelease),
                   val (lp::fxCompKnee), val (lp::fxCompMakeup), val (lp::fxCompMix) };
        layerFx[l].setParams (p);
    }
}

void Synth::updateMasterFx() noexcept
{
    auto v = [this] (int i) { return hostValue (masterFxParams[(size_t) i]); };
    auto sw = [&] (int i) { return on (v (i)); };
    auto ch = [&] (int i) { return idx (v (i)); };

    fx::MasterFxParams p;
    p.driveOn = sw (driveOn);
    p.drive = { fx::masterDriveType (ch (driveType)), v (driveDrive), v (driveTone), v (driveMix), v (driveOutput) };
    p.chorusOn = sw (chorusOn);
    p.chorus.mode = (fx::ChorusMode) juce::jlimit (0, 6, ch (chorusMode));
    p.chorus.rateHz = v (chorusRate);
    p.chorus.depth = v (chorusDepth);
    p.chorus.delayMs = 7.0f;
    p.chorus.width = 1.0f;
    p.chorus.mix = v (chorusMix);
    p.delayOn = sw (delayOn);
    p.delay.type = (fx::DelayType) juce::jlimit (0, 5, ch (delayType));
    const bool sync = sw (delaySync);
    p.delay.timeL = sync ? fx::divisionSeconds (ch (delayDivL), globals.bpm) : v (delayTimeL) * 0.001f;
    p.delay.timeR = p.delay.type != fx::DelayType::DualLR ? p.delay.timeL
                  : sync ? fx::divisionSeconds (ch (delayDivR), globals.bpm) : v (delayTimeR) * 0.001f;
    p.delay.feedback = v (delayFeedback);
    p.delay.hpHz = v (delayHp);
    p.delay.lpHz = v (delayLp);
    p.delay.width = v (delayWidth);
    p.delay.modulation = v (delayMod);
    p.delay.ducking = v (delayDucking);
    p.delay.mix = v (delayMix);
    p.reverbOn = sw (reverbOn);
    p.reverb = { (fx::ReverbType) juce::jlimit (0, 3, ch (reverbType)), v (reverbSize), v (reverbDecay), v (reverbPredelay),
                 v (reverbDamping), v (reverbDiffusion), v (reverbMod), v (reverbLowcut), v (reverbHighcut),
                 v (reverbWidth), v (reverbMix) };
    p.filterOn = sw (filterOn);
    p.filter = { sw (hpOn), sw (bpOn), sw (lpOn), v (hpFreq), v (hpRes), v (bpFreq), v (bpWidth), v (bpRes), v (lpFreq), v (lpRes) };
    masterFx.setParams (p);
}

void Synth::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midiIn, double bpm) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    buffer.clear();
    globals.bpm = bpm > 0.0 ? bpm : 120.0;
    readHostParams();

    auto it = midiIn.cbegin();
    const auto end = midiIn.cend();
    int pos = 0;
    while (pos < numSamples)
    {
        if (tickRemaining == 0)
        {
            controlTick();
            tickRemaining = controlBlock;
        }
        globals.tickRemaining = tickRemaining;
        while (it != end && (*it).samplePosition <= pos)
        {
            const auto meta = *it;
            sampleClockOffset = pos;
            handleMidi (meta.getMessage());
            ++it;
        }
        int len = juce::jmin (numSamples - pos, tickRemaining);
        if (it != end)
            len = juce::jmin (len, (*it).samplePosition - pos);
        len = juce::jmax (1, len);

        renderChunk (buffer.getArrayOfWritePointers(), numCh, pos, len);
        pos += len;
        tickRemaining -= len;
    }

    // Sum -> Master FX (Drive, Chorus, Delay, Reverb, Filter)
    updateMasterFx();
    if (masterFx.anyOn())
    {
        if (numCh > 1)
            masterFx.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
        else
            for (int s = 0; s < numSamples; s += (int) monoScratch.size())
            {
                const int len = juce::jmin ((int) monoScratch.size(), numSamples - s);
                float* m = buffer.getWritePointer (0) + s;
                std::copy (m, m + len, monoScratch.begin());
                masterFx.process (m, monoScratch.data(), len);
                for (int i = 0; i < len; ++i) m[i] = 0.5f * (m[i] + monoScratch[(size_t) i]);
            }
    }

    // Master Level (smoothed) -> true-peak limiter
    const float db = hostValue (masterVolume);
    masterGain.setTarget (db <= -59.99f ? 0.0f : juce::Decibels::decibelsToGain (db), numSamples);
    auto* l = buffer.getWritePointer (0);
    auto* r = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = masterGain.next();
        l[i] *= g;
        if (r != nullptr) r[i] *= g;
    }
    limiter.process (buffer.getArrayOfWritePointers(), numSamples);
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);
    sampleClock += (uint64_t) numSamples;
    wt::TableSlot::audioBlockDone();   // user wavetables replaced before this block can now be freed
}

void Synth::renderChunk (float* const* out, int numChannels, int start, int n) noexcept
{
    for (auto& st : layers)
    {
        std::fill (st.bufL.begin(), st.bufL.begin() + n, 0.0f);
        std::fill (st.bufR.begin(), st.bufR.begin() + n, 0.0f);
    }
    globals.tickRemaining = tickRemaining;
    for (auto& v : voices)
        if (v.state != Voice::State::Free)
            v.render (globals, 0, n);

    for (size_t l = 0; l < numLayers; ++l)
    {
        auto& st = layers[l];
        if (st.gainL.current == 0.0f && st.gainR.current == 0.0f && st.gainL.target == 0.0f && st.gainR.target == 0.0f)
            continue;
        layerFx[l].process (st.bufL.data(), st.bufR.data(), n);   // Engine -> Layer FX -> Layer mixer
        for (int i = 0; i < n; ++i)
        {
            const float a = st.bufL[(size_t) i] * st.gainL.next();
            const float b = st.bufR[(size_t) i] * st.gainR.next();
            if (numChannels > 1) { out[0][start + i] += a; out[1][start + i] += b; }
            else out[0][start + i] += 0.5f * (a + b);
        }
    }
}

// ------------------------------------------------------------------------------------------------ MIDI
void Synth::handleMidi (const juce::MidiMessage& m) noexcept
{
    const int ch = juce::jlimit (0, 15, m.getChannel() - 1);
    auto& cs = midi.channels[(size_t) ch];
    if (m.isNoteOn())
        noteOn (ch, m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        noteOff (ch, m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isPitchWheel())
        cs.bend = juce::jlimit (-1.0f, 1.0f, (float) (m.getPitchWheelValue() - 8192) / 8191.0f);
    else if (m.isChannelPressure())
        cs.pressure = (float) m.getChannelPressureValue() / 127.0f;
    else if (m.isAftertouch())
        cs.polyPressure[(size_t) m.getNoteNumber()] = (float) m.getAfterTouchValue() / 127.0f;
    else if (m.isController())
    {
        const int n = m.getControllerNumber();
        const bool down = m.getControllerValue() >= 64;
        cs.cc[(size_t) n] = (float) m.getControllerValue() / 127.0f;
        if (n == 64) pedal (ch, true, down);
        else if (n == 66) pedal (ch, false, down);
        else if (n == 120 || n == 123)
        {
            for (auto& v : voices)
                if (v.state != Voice::State::Free && (n == 120 || v.channel == ch || globals.mpe.enabled))
                {
                    if (n == 120) v.kill (globals); else releaseVoice (v, 0.0f);
                }
            for (auto& st : layers) st.stackSize = 0;
        }
    }
}

void Synth::noteOn (int ch, int note, float vel) noexcept
{
    if (vel <= 0.0f) { noteOff (ch, note, 0.0f); return; }
    const int v127 = juce::jlimit (1, 127, (int) std::lround (vel * 127.0f));
    uint16_t eligible = 0;
    for (size_t l = 0; l < numLayers; ++l)
    {
        const auto& st = layers[l];
        if (! st.audible) continue;
        if (! globals.mpe.enabled && st.midiCh > 0 && st.midiCh - 1 != ch) continue;
        if (note < st.keyLo || note > st.keyHi || v127 < st.velLo || v127 > st.velHi) continue;
        eligible = (uint16_t) (eligible | (1u << l));
    }
    const uint16_t mask = scaleLayersForNote (note, scaleEnabled, scaleMasks, eligible);
    for (int l = 0; l < numLayers; ++l)
        if (mask & (1u << l))
            layerNoteOn (l, ch, note, vel);
}

void Synth::startVoice (int v, const VoiceStart& s) noexcept
{
    auto& voice = voices[(size_t) v];
    if (voice.state == Voice::State::Free) voice.start (s, globals);
    else voice.steal (s, globals);
}

void Synth::layerNoteOn (int l, int ch, int note, float vel) noexcept
{
    auto& st = layers[(size_t) l];
    const auto& b = runtime[(size_t) l].base;
    const bool glideAlways = on (b[(size_t) lp::glideOn]) && idx (b[(size_t) lp::glideMode]) == 0;
    VoiceStart s { l, ch, note, vel, sampleClock + (uint64_t) sampleClockOffset, glideAlways ? st.lastPitch : -1.0f };

    if (! layerMono (l))
    {
        const int v = allocateVoice (l);
        if (v >= 0) startVoice (v, s);
        st.lastPitch = (float) note;
        return;
    }

    // Mono / Unison / Legato
    if (st.stackSize == (int) st.stack.size())
    {
        std::move (st.stack.begin() + 1, st.stack.end(), st.stack.begin());
        --st.stackSize;
    }
    st.stack[(size_t) st.stackSize++] = { ch, note, vel, ++noteOrder };

    const int prio = idx (b[(size_t) lp::notePriority]);
    int best = 0;
    for (int i = 1; i < st.stackSize; ++i)
    {
        const auto& e = st.stack[(size_t) i]; const auto& c = st.stack[(size_t) best];
        const bool better = prio == 0 ? e.order > c.order : prio == 1 ? e.order < c.order
                          : prio == 2 ? e.note < c.note : e.note > c.note;
        if (better) best = i;
    }
    const auto chosen = st.stack[(size_t) best];

    const int mv = st.monoVoice;
    const bool valid = mv >= 0 && voices[(size_t) mv].state != Voice::State::Free && voices[(size_t) mv].layer == l;
    if (valid)
    {
        auto& voice = voices[(size_t) mv];
        if (voice.note == chosen.note && voice.gate) return;
        const bool held = voice.gate;
        const int mode = idx (b[(size_t) lp::voiceMode]);
        const bool retrig = ! held || (mode != 3 && on (b[(size_t) lp::voiceRetrigger]));
        const bool glide = on (b[(size_t) lp::glideOn]) && (idx (b[(size_t) lp::glideMode]) == 0 || held);
        voice.channel = chosen.channel;
        voice.legato (chosen.note, chosen.velocity, retrig, glide, globals);
    }
    else
    {
        const int v = allocateVoice (l);
        if (v < 0) return;
        s.note = chosen.note; s.channel = chosen.channel; s.velocity = chosen.velocity;
        startVoice (v, s);
        st.monoVoice = v;
    }
    st.lastPitch = (float) chosen.note;
}

void Synth::monoNoteOff (int l, int ch, int note, float relVel) noexcept
{
    auto& st = layers[(size_t) l];
    int found = -1;
    for (int i = 0; i < st.stackSize; ++i)
        if (st.stack[(size_t) i].channel == ch && st.stack[(size_t) i].note == note) { found = i; break; }
    if (found < 0) return;
    std::move (st.stack.begin() + found + 1, st.stack.begin() + st.stackSize, st.stack.begin() + found);
    --st.stackSize;

    const int mv = st.monoVoice;
    if (mv < 0 || voices[(size_t) mv].state == Voice::State::Free || voices[(size_t) mv].layer != l)
        return;
    auto& voice = voices[(size_t) mv];
    if (st.stackSize == 0)
    {
        if (voice.note == note) releaseVoice (voice, relVel);
        return;
    }
    const auto& b = runtime[(size_t) l].base;
    const int prio = idx (b[(size_t) lp::notePriority]);
    int best = 0;
    for (int i = 1; i < st.stackSize; ++i)
    {
        const auto& e = st.stack[(size_t) i]; const auto& c = st.stack[(size_t) best];
        const bool better = prio == 0 ? e.order > c.order : prio == 1 ? e.order < c.order
                          : prio == 2 ? e.note < c.note : e.note > c.note;
        if (better) best = i;
    }
    const auto& chosen = st.stack[(size_t) best];
    if (voice.note != chosen.note)
    {
        voice.channel = chosen.channel;
        voice.legato (chosen.note, chosen.velocity, false, on (b[(size_t) lp::glideOn]), globals);
        st.lastPitch = (float) chosen.note;
    }
}

void Synth::noteOff (int ch, int note, float relVel) noexcept
{
    for (int l = 0; l < numLayers; ++l)
        if (layers[(size_t) l].stackSize > 0 && layerMono (l))
            monoNoteOff (l, ch, note, relVel);

    for (auto& v : voices)
        if (v.state != Voice::State::Free && v.gate && v.channel == ch && v.note == note && ! layerMono (v.layer))
            releaseVoice (v, relVel);
}

void Synth::releaseVoice (Voice& v, float relVel) noexcept
{
    const auto& cs = midi.channels[(size_t) globals.mpe.globalChannel (v.channel)];
    const auto& b = runtime[(size_t) v.layer].base;
    const bool holdSus = cs.sustain && on (b[(size_t) lp::sustainOn]);
    const bool holdSos = v.sostenutoLatched && cs.sostenuto && on (b[(size_t) lp::sostenutoOn]);
    if (holdSus || holdSos)
    {
        v.gate = false;
        v.sustained = true;
        return;
    }
    v.release (relVel, globals);
}

void Synth::pedal (int ch, bool sustain, bool down) noexcept
{
    auto& cs = midi.channels[(size_t) ch];
    if (sustain) cs.sustain = down; else cs.sostenuto = down;

    for (auto& v : voices)
    {
        if (v.state == Voice::State::Free || globals.mpe.globalChannel (v.channel) != ch)
            continue;
        if (! sustain && down && v.gate)
            v.sostenutoLatched = true;
        if (! down)
        {
            if (! sustain) v.sostenutoLatched = false;
            if (v.sustained && ! v.gate)
            {
                v.sustained = false;
                releaseVoice (v, 0.0f);
            }
        }
    }
}

int Synth::allocateVoice (int l) noexcept
{
    const bool steal = on (runtime[(size_t) l].base[(size_t) lp::voiceSteal]);
    const int maxV = juce::jmin (layers[(size_t) l].maxVoices, pool);
    if (layerVoiceCount (l) >= maxV)
        return steal ? pickSteal (l) : -1;
    for (int i = 0; i < pool; ++i)
        if (voices[(size_t) i].state == Voice::State::Free)
            return i;
    return steal ? pickSteal (-1) : -1;
}

// Global stealing (§1.2): oldest voice first, then the quietest.
int Synth::pickSteal (int layer) const noexcept
{
    int best = -1;
    for (int i = 0; i < maxPoolVoices; ++i)
    {
        const auto& v = voices[(size_t) i];
        if (v.state != Voice::State::Playing) continue;
        if (layer >= 0 ? v.layer != layer : i >= pool) continue;
        if (best < 0) { best = i; continue; }
        const auto& b = voices[(size_t) best];
        if (v.startTime < b.startTime || (v.startTime == b.startTime && v.currentLevel < b.currentLevel))
            best = i;
    }
    return best;
}

// ------------------------------------------------------------------------------------------------ queries
Synth::VoiceInfo Synth::voiceInfo (int i) const noexcept
{
    const auto& v = voices[(size_t) i];
    return { v.state != Voice::State::Free, v.layer, v.note, v.gate, v.currentLevel };
}

int Synth::activeVoiceCount() const noexcept
{
    int c = 0;
    for (const auto& v : voices) c += v.state != Voice::State::Free ? 1 : 0;
    return c;
}

// ------------------------------------------------------------------------------------------------ message thread
int Synth::hostParamIndex (const juce::String& id) const
{
    if (auto* p = apvts.getParameter (id)) return p->getParameterIndex();
    return -1;
}

bool Synth::assignMacro (int macro, ParamTarget t, float minNorm, float maxNorm)
{
    auto& m = macroMap.edit();
    for (int i = 0; i < m.count; ++i)
        if (m.items[(size_t) i].macro == macro && m.items[(size_t) i].target == t)
        {
            m.items[(size_t) i].min = minNorm; m.items[(size_t) i].max = maxNorm;
            macroMap.publish();
            return true;
        }
    if (m.count >= maxMacroAssignments || ! t.isValid()) return false;
    m.items[(size_t) m.count++] = { macro, t, juce::jlimit (0.0f, 1.0f, minNorm), juce::jlimit (0.0f, 1.0f, maxNorm) };
    macroMap.publish();
    return true;
}

void Synth::clearMacroAssignments (int macro)
{
    auto& m = macroMap.edit();
    int w = 0;
    for (int i = 0; i < m.count; ++i)
        if (m.items[(size_t) i].macro != macro) m.items[(size_t) w++] = m.items[(size_t) i];
    m.count = w;
    macroMap.publish();
}

int Synth::assignAutomation (int layer, int lpId)
{
    auto& a = automationMap.edit();
    const ParamTarget t { layer, lpId };
    int slot = -1;
    for (int s = 0; s < numAutomationSlots && slot < 0; ++s)
        if (a.slots[(size_t) s] == t) slot = s;
    for (int s = 0; s < numAutomationSlots && slot < 0; ++s)
        if (! a.slots[(size_t) s].isValid()) slot = s;
    if (slot < 0) return -1;
    autoSlots[(size_t) slot]->setValueNotifyingHost (paramRange (lpId).toNorm (stores[(size_t) layer].get (lpId)));
    a.slots[(size_t) slot] = t;
    automationMap.publish();
    return slot;
}

void Synth::clearAutomation (int slot)
{
    auto& a = automationMap.edit();
    const auto t = a.slots[(size_t) slot];
    if (t.layer >= 0)   // keep the value the slot was driving
        stores[(size_t) t.layer].set (t.id, paramRange (t.id).fromNorm (autoSlots[(size_t) slot]->getValue()));
    a.slots[(size_t) slot] = {};
    automationMap.publish();
}

juce::ValueTree Synth::copyLayer (int l) const
{
    juce::ValueTree t ("LAYER_PRESET");
    for (auto* arr : layerIdArrays)
        if (auto* p = apvts.getParameter ((*arr)[(size_t) l]))
            t.setProperty (layerSuffix ((*arr)[(size_t) l]), p->getValue(), nullptr);
    t.appendChild (stores[(size_t) l].toTree ("ENGINE"), nullptr);
    return t;
}

void Synth::pasteLayer (int l, const juce::ValueTree& t)
{
    if (! t.hasType ("LAYER_PRESET")) return;
    for (auto* arr : layerIdArrays)
        if (auto* p = apvts.getParameter ((*arr)[(size_t) l]))
        {
            const auto key = layerSuffix ((*arr)[(size_t) l]);
            if (t.hasProperty (key)) p->setValueNotifyingHost ((float) t.getProperty (key));
        }
    stores[(size_t) l].fromTree (t.getChildWithName ("ENGINE"));
}

void Synth::setUserWavetable (int layer, int slot, std::vector<float> frames, int numFrames)
{
    if (layer >= 0 && layer < numLayers && slot >= 0 && slot < wt::numUserSlots)
        stores[(size_t) layer].setUserTable (slot, std::move (frames), numFrames);
}

void Synth::initLayer (int l)
{
    for (auto* arr : layerIdArrays)
        if (auto* p = apvts.getParameter ((*arr)[(size_t) l]))
            p->setValueNotifyingHost (p->getDefaultValue());
    stores[(size_t) l].init();
}

bool Synth::saveLayerPreset (int l, const juce::File& f) const
{
    if (auto xml = copyLayer (l).createXml()) return xml->writeTo (f);
    return false;
}

bool Synth::loadLayerPreset (int l, const juce::File& f)
{
    if (auto xml = juce::XmlDocument::parse (f))
    {
        pasteLayer (l, juce::ValueTree::fromXml (*xml));
        return true;
    }
    return false;
}

juce::ValueTree Synth::saveState() const
{
    juce::ValueTree core ("CORE");
    for (int l = 0; l < numLayers; ++l)
    {
        auto t = stores[(size_t) l].toTree ("LAYER");
        t.setProperty ("index", l, nullptr);
        core.appendChild (t, nullptr);
    }
    juce::ValueTree macrosTree ("MACROS");
    const auto& m = macroMap.latest();
    for (int i = 0; i < m.count; ++i)
    {
        const auto& a = m.items[(size_t) i];
        juce::ValueTree e ("ASSIGN");
        e.setProperty ("macro", a.macro, nullptr);
        e.setProperty ("layer", a.target.layer, nullptr);
        e.setProperty ("param", a.target.layer == ParamTarget::host ? juce::String (hostParams[(size_t) a.target.id]->getParameterID())
                                                                    : juce::String (lp::descs[(size_t) a.target.id].id), nullptr);
        e.setProperty ("min", a.min, nullptr);
        e.setProperty ("max", a.max, nullptr);
        macrosTree.appendChild (e, nullptr);
    }
    core.appendChild (macrosTree, nullptr);
    juce::ValueTree autoTree ("AUTOMATION");
    const auto& am = automationMap.latest();
    for (int s = 0; s < numAutomationSlots; ++s)
        if (am.slots[(size_t) s].layer >= 0)
        {
            juce::ValueTree e ("SLOT");
            e.setProperty ("slot", s, nullptr);
            e.setProperty ("layer", am.slots[(size_t) s].layer, nullptr);
            e.setProperty ("param", juce::String (lp::descs[(size_t) am.slots[(size_t) s].id].id), nullptr);
            autoTree.appendChild (e, nullptr);
        }
    core.appendChild (autoTree, nullptr);
    return core;
}

void Synth::loadState (const juce::ValueTree& core)
{
    auto findLp = [] (const juce::String& id)
    {
        for (int i = 0; i < lp::count; ++i)
            if (id == lp::descs[(size_t) i].id) return i;
        return -1;
    };
    for (auto& s : stores) s.init();
    for (auto t : core)
        if (t.hasType ("LAYER"))
        {
            const int l = t.getProperty ("index", -1);
            if (l >= 0 && l < numLayers) stores[(size_t) l].fromTree (t);
        }

    auto& m = macroMap.edit();
    m.count = 0;
    for (auto e : core.getChildWithName ("MACROS"))
    {
        const int layer = e.getProperty ("layer", ParamTarget::none);
        const auto pid = e.getProperty ("param").toString();
        const int id = layer == ParamTarget::host ? hostParamIndex (pid) : findLp (pid);
        if (id < 0 || m.count >= maxMacroAssignments || layer < ParamTarget::host || layer >= numLayers) continue;
        m.items[(size_t) m.count++] = { (int) e.getProperty ("macro", 0), { layer, id }, (float) e.getProperty ("min", 0.0f), (float) e.getProperty ("max", 1.0f) };
    }
    macroMap.publish();

    auto& a = automationMap.edit();
    a.slots.fill ({});
    for (auto e : core.getChildWithName ("AUTOMATION"))
    {
        const int s = e.getProperty ("slot", -1), layer = e.getProperty ("layer", -1);
        const int id = findLp (e.getProperty ("param").toString());
        if (s >= 0 && s < numAutomationSlots && layer >= 0 && layer < numLayers && id >= 0)
            a.slots[(size_t) s] = { layer, id };
    }
    automationMap.publish();
}
} // namespace chroma
