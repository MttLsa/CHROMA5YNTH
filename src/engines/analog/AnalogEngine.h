#pragma once
#include "engines/IEngine.h"
#include "shared/Oversampler.h"
#include <array>
#include <cstdint>
#include <vector>

namespace chroma
{
// ENGINE ANALOG (§3A): 3 band-limited oscillators (4-point BLEP/BLAMP, internal oversampling + halfband
// decimation), SuperSaw, hard sync 2→1, through-zero FM 3→1 / 2→1, ring 1×2, Sub, Noise, mixer with
// per-source Filter/Bypass routing. Unused sources cost no CPU.
class AnalogEngine final : public IEngine
{
public:
    enum Wave { Sine, Triangle, Saw, Square, Pulse, SuperSaw, Half, Narrow };
    static constexpr int numOsc = 3, maxSuper = 7, maxSubVoices = maxUnison * maxSuper, maxOversampling = 8;

    void prepare (double sampleRate, int maxBlockSize, int maxVoices) override;
    void reset() override;
    void noteOn (int voice, const EngineNote&) override;
    void noteOff (int) override {}
    void kill (int voice) override;
    void setOversampling (int factor) override;
    bool render (int voice, const EngineVoiceParams&, float* left, float* right, int n) noexcept override;

    int oversampling() const noexcept { return os; }

    // Waveform description used by the band-limited stepper (public for tests).
    struct Shape
    {
        int wave = Saw;
        float m = 0.0f, w = 0.5f;         // wave mod, pulse width
        int nbp = 0;
        std::array<float, 2> bp {}, dv {}, ds {};   // breakpoints: value jump / slope jump (per unit phase)
        float value (double p) const noexcept;
        float slope (double p) const noexcept;
    };
    static Shape makeShape (int wave, float waveMod, float pulseWidth) noexcept;

private:
    struct Osc
    {
        std::array<double, maxSubVoices> phase {};
        std::array<float, 4> tailL {}, tailR {};
        float gainL = 0.0f, gainR = 0.0f;   // mixer gains (ramped)
        bool active = false;
    };
    struct Noise
    {
        uint32_t rngL = 1, rngR = 2;
        std::array<float, 3> pinkL {}, pinkR {};
        float lpL = 0.0f, lpR = 0.0f, gainL = 0.0f, gainR = 0.0f;
    };
    struct VoiceState
    {
        std::array<Osc, numOsc> osc {};
        double subPhase = 0.0;
        std::array<float, 4> subTail {};
        float subGain = 0.0f, ring = 0.0f;
        bool subActive = false, pendingStart = false;
        Noise noise;
        std::array<Decimator, 4> dec {};   // filter L/R, bypass L/R
        std::array<int, 2> busHold {};     // base-rate samples left before an idle bus stops (FIR tail)
        uint32_t seed = 1;
    };

    struct OscRender
    {
        Shape shape;
        int subCount = 1, unison = 1, superCount = 1;
        std::array<float, maxSubVoices> inc0 {}, inc1 {}, gl {}, gr {};
        float resetPhase = 0.0f;
    };

    void setupOsc (OscRender&, const EngineVoiceParams&, int k, float pitch0, float pitch1) const noexcept;
    void renderOsc (VoiceState&, int k, const OscRender&, int N, const float* fm, bool recordSync, bool useSync) noexcept;
    void renderSub (VoiceState&, int wave, float pitch0, float pitch1, int N) noexcept;
    void renderNoise (VoiceState&, const EngineVoiceParams&, float* l, float* r, int n, float gl, float gr) noexcept;
    bool renderBlock (VoiceState&, const EngineVoiceParams&, float* left, float* right, float* byL, float* byR,
                      int n, float pitch0, float pitch1) noexcept;

    double sr = 48000.0;
    int os = 2, maxBlock = 32, maxN = 64;
    std::vector<VoiceState> voices;

    // scratch (shared by all voices; rendering is sequential)
    std::array<std::vector<float>, numOsc> oscL, oscR;
    std::vector<float> subBuf, busFL, busFR, busBL, busBR, work, fmBuf, syncX;
};
} // namespace chroma
