#pragma once
#include "Chorus.h"
#include "Delay.h"
#include "Distortion.h"
#include "Dynamics.h"
#include "Modulation.h"
#include "Reverb.h"

namespace chroma::fx
{
// One FX in a chain: On/Off with a 5 ms crossfade; off (after the fade) = not processed (zero CPU).
// Switching on resets the FX state.
template <typename Fx>
struct Slot
{
    Fx fx;
    Bypass bypass;

    void prepareSlot (double sr) { bypass.prepare (sr); bypass.reset (false); }
    bool running() const noexcept { return ! bypass.idle(); }

    void process (float* l, float* r, int n) noexcept
    {
        if (bypass.idle()) return;
        if (bypass.justStarting()) fx.reset();
        if (bypass.steady()) { fx.process (l, r, n); return; }
        std::array<float, chunk> dl, dr;
        std::copy (l, l + n, dl.begin());
        std::copy (r, r + n, dr.begin());
        fx.process (l, r, n);
        for (int i = 0; i < n; ++i)
        {
            const float g = bypass.next();
            l[i] = dl[(size_t) i] + g * (l[i] - dl[(size_t) i]);
            r[i] = dr[(size_t) i] + g * (r[i] - dr[(size_t) i]);
        }
    }
    static constexpr int chunk = 64;
};

// Sleep detector: a chain whose input has been silent and whose output has decayed below
// -120 dBFS for longer than any tail (delay <= 2 s, reverb) stops processing.
struct SilenceGate
{
    int quiet = 0, limit = 0;
    void prepare (double sr) noexcept { limit = (int) (2.5 * sr); quiet = 0; }
    static bool silent (const float* x, int n, float thr) noexcept
    {
        for (int i = 0; i < n; ++i) if (std::fabs (x[i]) > thr) return false;
        return true;
    }
    bool asleep() const noexcept { return quiet >= limit; }
    void wake() noexcept { quiet = 0; }
    void update (const float* l, const float* r, int n) noexcept
    {
        if (silent (l, n, 1.0e-6f) && silent (r, n, 1.0e-6f)) quiet = std::min (limit, quiet + n);
        else quiet = 0;
    }
};

// LAYER FX (§2): Distortion -> Chorus -> Phaser -> Flanger -> Delay -> Reverb -> EQ -> Compressor.
struct LayerFxParams
{
    bool distOn = false, chorusOn = false, phaserOn = false, flangerOn = false, delayOn = false, reverbOn = false,
         eqOn = false, compOn = false;
    DistortionParams dist; ChorusParams chorus; PhaserParams phaser; FlangerParams flanger;
    DelayParams delay; bool delayPingPong = false; ReverbParams reverb; EqParams eq; CompressorParams comp;
};

class LayerFxChain
{
public:
    void prepare (double sr)
    {
        dist.fx.prepare (sr); chorus.fx.prepare (sr); phaser.fx.prepare (sr); flanger.fx.prepare (sr);
        delay.fx.prepare (sr); reverb.fx.prepare (sr, false); eq.fx.prepare (sr); comp.fx.prepare (sr);
        dist.prepareSlot (sr); chorus.prepareSlot (sr); phaser.prepareSlot (sr); flanger.prepareSlot (sr);
        delay.prepareSlot (sr); reverb.prepareSlot (sr); eq.prepareSlot (sr); comp.prepareSlot (sr);
        gate.prepare (sr);
    }

    void setParams (const LayerFxParams& p) noexcept
    {
        dist.bypass.set (p.distOn); chorus.bypass.set (p.chorusOn); phaser.bypass.set (p.phaserOn);
        flanger.bypass.set (p.flangerOn); delay.bypass.set (p.delayOn); reverb.bypass.set (p.reverbOn);
        eq.bypass.set (p.eqOn); comp.bypass.set (p.compOn);
        if (p.distOn) dist.fx.setParams (p.dist);
        if (p.chorusOn) chorus.fx.setParams (p.chorus);
        if (p.phaserOn) phaser.fx.setParams (p.phaser);
        if (p.flangerOn) flanger.fx.setParams (p.flanger);
        if (p.delayOn) { delay.fx.pingPong = p.delayPingPong; delay.fx.setParams (p.delay); }
        if (p.reverbOn) reverb.fx.setParams (p.reverb);
        if (p.eqOn) eq.fx.setParams (p.eq);
        if (p.compOn) comp.fx.setParams (p.comp);
    }

    bool anyOn() const noexcept
    {
        return dist.running() || chorus.running() || phaser.running() || flanger.running() || delay.running()
            || reverb.running() || eq.running() || comp.running();
    }
    bool isAsleep() const noexcept { return gate.asleep(); }

    // n <= 64. In place on the layer bus.
    void process (float* l, float* r, int n) noexcept
    {
        if (! anyOn()) { gate.wake(); return; }
        const bool inputSilent = SilenceGate::silent (l, n, 0.0f) && SilenceGate::silent (r, n, 0.0f);
        if (inputSilent && gate.asleep()) return;
        if (! inputSilent) gate.wake();
        dist.process (l, r, n); chorus.process (l, r, n); phaser.process (l, r, n); flanger.process (l, r, n);
        delay.process (l, r, n); reverb.process (l, r, n); eq.process (l, r, n); comp.process (l, r, n);
        if (inputSilent) gate.update (l, r, n);
    }

private:
    Slot<Distortion> dist; Slot<Chorus> chorus; Slot<Phaser> phaser; Slot<Flanger> flanger;
    Slot<Delay> delay; Slot<Reverb> reverb; Slot<Eq3> eq; Slot<Compressor> comp;
    SilenceGate gate;
};

// MASTER FX (§4): Drive -> Chorus -> Delay -> Reverb -> Filter. Same DSP modules as the Layer FX.
struct MasterFxParams
{
    bool driveOn = false, chorusOn = false, delayOn = false, reverbOn = false, filterOn = false;
    DistortionParams drive; ChorusParams chorus; DelayParams delay; ReverbParams reverb; MasterFilterParams filter;
};

class MasterFxChain
{
public:
    void prepare (double sr)
    {
        drive.fx.prepare (sr); chorus.fx.prepare (sr); delay.fx.prepare (sr); reverb.fx.prepare (sr, true); filter.fx.prepare (sr);
        drive.prepareSlot (sr); chorus.prepareSlot (sr); delay.prepareSlot (sr); reverb.prepareSlot (sr); filter.prepareSlot (sr);
        gate.prepare (sr);
    }

    void setParams (const MasterFxParams& p) noexcept
    {
        drive.bypass.set (p.driveOn); chorus.bypass.set (p.chorusOn); delay.bypass.set (p.delayOn);
        reverb.bypass.set (p.reverbOn); filter.bypass.set (p.filterOn);
        if (p.driveOn) drive.fx.setParams (p.drive);
        if (p.chorusOn) chorus.fx.setParams (p.chorus);
        if (p.delayOn) delay.fx.setParams (p.delay);
        if (p.reverbOn) reverb.fx.setParams (p.reverb);
        if (p.filterOn) filter.fx.setParams (p.filter);
    }

    bool anyOn() const noexcept { return drive.running() || chorus.running() || delay.running() || reverb.running() || filter.running(); }

    // Any length; processed in sub-blocks of 64.
    void process (float* l, float* r, int n) noexcept
    {
        if (! anyOn()) { gate.wake(); return; }
        for (int s = 0; s < n; s += 64)
        {
            const int len = std::min (64, n - s);
            float* a = l + s; float* b = r + s;
            const bool inputSilent = SilenceGate::silent (a, len, 0.0f) && SilenceGate::silent (b, len, 0.0f);
            if (inputSilent && gate.asleep()) continue;
            if (! inputSilent) gate.wake();
            drive.process (a, b, len); chorus.process (a, b, len); delay.process (a, b, len);
            reverb.process (a, b, len); filter.process (a, b, len);
            if (inputSilent) gate.update (a, b, len);
        }
    }

private:
    Slot<Distortion> drive; Slot<Chorus> chorus; Slot<Delay> delay; Slot<Reverb> reverb; Slot<MasterFilter> filter;
    SilenceGate gate;
};
} // namespace chroma::fx
