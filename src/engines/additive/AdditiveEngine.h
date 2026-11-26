#pragma once
#include "engines/IEngine.h"
#include "AdditiveSpectrum.h"
#include "shared/Envelope.h"
#include <array>
#include <cstdint>
#include <vector>

namespace chroma
{
// ENGINE ADDITIVE (§3B): up to 256 sine partials per voice from two spectra (factory shape or the
// layer's Custom table) with Morph, global spectral shaping (Distribution, Tilt, Odd/Even, Balance,
// Brightness/Slope, Density, Inharmonicity, Stretch, Shift), per-partial ENV, spectral ENV, FM / AM,
// stereo placement. Oscillator bank: phase-accumulator partials evaluated with a polynomial sine,
// vectorised along samples; FM, pitch glide and amplitude ramps are folded into per-block prefix
// sums so every partial costs a handful of multiply-adds per sample. Partials above Nyquist or at
// level 0 are skipped (zero CPU). Band-limited by construction: no oversampling.
class AdditiveEngine final : public IEngine
{
public:
    static constexpr int maxPartials = add::maxPartials, subBlock = 32;
    static constexpr float outputScale = 0.5f;

    // Custom tables of the layer (A and B), read lock-free. Null = Saw.
    void setCustomSpectra (const add::CustomSpectrum* a, const add::CustomSpectrum* b) noexcept { custom = { a, b }; }

    void prepare (double sampleRate, int maxBlockSize, int maxVoices) override;
    void reset() override;
    void noteOn (int voice, const EngineNote&) override;
    void noteOff (int voice) override;
    void kill (int voice) override;
    bool render (int voice, const EngineVoiceParams&, float* left, float* right, int n) noexcept override;

    // Partial ENV level at scaled time ts (seconds × n^Scaling) while held: 0..1.
    static float partialEnvHeld (float ts, float attack, float decay, float sustain, int curve) noexcept;
    static float envShape (float x, int curve) noexcept;

private:
    struct VoiceState
    {
        std::vector<float> phase;                      // [maxUnison][maxPartials], cycles 0..1
        std::array<float, maxPartials> gL {}, gR {};   // amplitudes reached at the end of the last block
        std::array<float, maxPartials> relLevel {};    // partial ENV level at note-off
        Envelope senv;
        double t = 0.0, tRel = 0.0;                    // seconds since note-on / at note-off
        double fmPhase = 0.0, amPhase = 0.0;
        float fmY1 = 0.0f, fmY2 = 0.0f;
        int note = 60, unison = 1, lastCount = 0;
        uint32_t rng = 1;
        bool active = false, started = false, pendingStart = false, pendingRelease = false, released = false,
             legato = false, relCaptured = false;
    };

    void start (VoiceState&, const EngineVoiceParams&, bool retrigger) noexcept;
    void renderBlock (VoiceState&, const EngineVoiceParams&, float* left, float* right, int n, float p0, float p1) noexcept;
    void buildSpectrum (const EngineVoiceParams&, int count, int shapeCount, float morph, float extraTilt) noexcept;
    float randomPhase (VoiceState&) noexcept;

    double sr = 48000.0;
    std::vector<VoiceState> voices;
    std::array<const add::CustomSpectrum*, 2> custom { nullptr, nullptr };

    // Scratch (rendering is sequential): spectrum after global shaping, per partial.
    add::SpectrumData specA, specB;
    std::array<float, maxPartials> sBase {}, sFixed {}, sFineMul {}, sOffHz {}, sAmp {}, sPan {}, sLr {}, sPhase {};
    std::array<float, maxPartials> sRf {}, sFx {}, sDevW {}, sPanL {}, sPanR {};
    std::array<float, maxPartials> tL {}, tR {}, devHz {};
    std::array<uint32_t, 24> cacheKey {};
    bool cacheValid = false;
    std::array<float, maxPartials> idxLog2 {}, density {};
    std::array<float, subBlock + 1> rampI {}, rampT2 {}, fmSum {};
    std::array<std::array<float, subBlock>, 4> bus {};   // L/R × (odd, even) partials
    std::array<float, subBlock> amMod {};
};
} // namespace chroma
