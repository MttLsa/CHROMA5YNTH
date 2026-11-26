#pragma once
#include "engines/IEngine.h"
#include "engines/pd/PdCurve.h"
#include "shared/Envelope.h"
#include "shared/Oversampler.h"
#include <array>
#include <cstdint>
#include <vector>

namespace chroma
{
// ENGINE PHASE DISTORTION (§3E): 2 oscillator lines whose timbre comes from a distorted phase map read
// through a cosine (or a windowed resonance for Reso 1/2/3). The master phase never jumps: modulation only
// changes the map, ramped per sample, so the output stays continuous. The map runs oversampled (default
// 2×, halfband decimation) and its steepest segment is limited by the note frequency (band-limited knee).
// Per line: PD ENV, Shape/Morph/Curve/Symmetry/Asymmetry/Offset/Warp/Window/Mix; Ring and Noise mod of
// line 2. Lines that are off cost no CPU.
class PdEngine final : public IEngine
{
public:
    enum Wave { Saw, Square, Pulse, DoubleSine, SawPulse, Reso1, Reso2, Reso3 };
    enum Shape { Linear, Smooth, Ripple, Steps, Custom, numShapes };
    static constexpr int numLines = 2, maxOversampling = 8;

    // Per-sample controls of one line's phase map (ramped between control blocks).
    struct Ctl
    {
        float amount = 0.0f, morph = 0.0f, curveExp = 1.0f, symmetry = 0.0f, asymmetry = 0.0f, offset = 0.0f,
              warpExp = 1.0f, window = 0.0f, mix = 1.0f, kneeMin = 0.01f, resoMax = 16.0f,
              phase = 0.0f;   // oscillator phase offset in cycles (applied by the engine before the map)
    };
    static constexpr int numCtl = 12;

    // Phase map of one line prepared once per sample (knots depend only on the controls).
    struct Map
    {
        int wave = Saw, shapeA = Linear, shapeB = Linear, numKnots = 2;
        std::array<float, 5> kx {}, ky {};
        float amount = 0.0f, morph = 0.0f, curveExp = 1.0f, asymKnee = 0.5f, warpExp = 1.0f, offset = 0.0f,
              window = 0.0f, mix = 1.0f, reso = 1.0f, windowKnee = 0.5f;
        const pd::PhaseCurve* custom = nullptr;
    };
    static void prepareMap (Map&, int wave, int shape, const Ctl&, const pd::PhaseCurve*) noexcept;
    static float mapPhase (const Map&, float ph) noexcept;     // distorted phase (before reading)
    static float evaluate (const Map&, float ph) noexcept;     // output sample

    // Distorted phase and one output sample for phase ph in [0, 1) (convenience for tests).
    static float distortedPhase (int wave, int shape, const Ctl&, float ph, const pd::PhaseCurve*) noexcept;
    static float lineValue (int wave, int shape, const Ctl&, float ph, const pd::PhaseCurve*) noexcept;

    void setCustomCurve (const pd::PhaseCurve* c) noexcept { custom = c; }

    void prepare (double sampleRate, int maxBlockSize, int maxVoices) override;
    void reset() override;
    void noteOn (int voice, const EngineNote&) override;
    void noteOff (int voice) override;
    void kill (int voice) override;
    void setOversampling (int factor) override;
    bool render (int voice, const EngineVoiceParams&, float* left, float* right, int n) noexcept override;

    int oversampling() const noexcept { return os; }

private:
    struct LineTargets
    {
        bool on = false;
        int wave = Saw, shape = Linear;
        Ctl ctl;
        std::array<float, maxUnison> inc {}, gl {}, gr {};
    };
    struct LineState
    {
        std::array<double, maxUnison> phase {};
        Envelope env;
        LineTargets cur;        // values reached at the end of the previous block
        bool active = false;
    };
    struct VoiceState
    {
        std::array<LineState, numLines> line {};
        std::array<Decimator, 2> dec {};
        float ring = 0.0f, noise = 0.0f, noiseCoef = 0.0f, noiseGain = 0.0f, noiseLp = 0.0f;
        float dcxL = 0.0f, dcyL = 0.0f, dcxR = 0.0f, dcyR = 0.0f;
        uint32_t rng = 0x9e3779b9u;
        int note = 60, unison = 1, tail = 0;
        float velocity = 1.0f;
        bool started = false, pendingStart = false, pendingRelease = false, legato = false, first = true;
    };

    void start (VoiceState&, const EngineVoiceParams&, bool retrigger) noexcept;
    void computeTargets (VoiceState&, const EngineVoiceParams&, int n, float pitch, std::array<LineTargets, numLines>&,
                         float& ring, float& noise, float& noiseCoef, float& noiseGain) noexcept;
    void renderBlock (VoiceState&, const EngineVoiceParams&, float* left, float* right, int n, float pitch) noexcept;

    double sr = 48000.0;
    int os = 2, maxBlock = 32;
    std::vector<VoiceState> voices;
    std::vector<float> busL, busR, work;
    const pd::PhaseCurve* custom = nullptr;
};
} // namespace chroma
