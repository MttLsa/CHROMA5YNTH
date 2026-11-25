#pragma once
#include "engines/IEngine.h"
#include "shared/Envelope.h"
#include "shared/Oversampler.h"
#include <array>
#include <vector>

namespace chroma
{
// ENGINE FM6 (§3C): 6 phase-modulation operators with per-operator ENV, key scaling, velocity / AM
// sensitivity, 8 sine-derived waves, hard sync to the fundamental; 32 classic algorithms + Custom
// 6×6 matrix, Algorithm Morph, pitch ENV, layer LFO 1/2 depths. Internal oversampling (default 2×)
// with halfband decimation. Disabled / silent operators cost no CPU.
class FmEngine final : public IEngine
{
public:
    static constexpr int numOps = 6, numAlgorithms = 32, customAlgorithm = 32, maxOversampling = 8;
    static constexpr float modScale = 2.0f;   // cycles of peak phase deviation for a full-level modulator
    static constexpr float fbScale = 0.3f;    // cycles of feedback for Feedback = 100%

    // Routing of one algorithm: [destination][source] (0-based operators).
    struct Algorithm
    {
        std::array<std::array<float, numOps>, numOps> conn {};   // operator modulation (diagonal = feedback)
        std::array<std::array<float, numOps>, numOps> fb {};     // where each op's Feedback param applies
        std::array<float, numOps> carrier {};
        int feedbackOp = 5;                                       // op carrying the classic feedback (sysex import)
    };
    static const Algorithm& classic (int index) noexcept;          // 0..31

    static float levelToAmp (float level) noexcept;               // 0..1 log law (−6 dB per 1/12)
    static float keyScale (float level, int note, int breakpoint, float leftDepth, float rightDepth,
                           int leftCurve, int rightCurve) noexcept;
    static float wave (int type, double phaseCycles) noexcept;

    void prepare (double sampleRate, int maxBlockSize, int maxVoices) override;
    void reset() override;
    void noteOn (int voice, const EngineNote&) override;
    void noteOff (int voice) override;
    void kill (int voice) override;
    void setOversampling (int factor) override;
    bool render (int voice, const EngineVoiceParams&, float* left, float* right, int n) noexcept override;

    int oversampling() const noexcept { return os; }

private:
    using Matrix = std::array<std::array<float, numOps>, numOps>;

    struct VoiceState
    {
        std::array<std::array<double, numOps>, maxUnison> phase {};
        std::array<std::array<float, numOps>, maxUnison> h1 {}, h2 {};   // last two outputs (feedback / delayed links)
        std::array<double, maxUnison> fundPhase {};
        std::array<Envelope, numOps> env {};
        Envelope pitchEnv;
        std::array<float, numOps> amp {};                 // current operator amplitudes
        Matrix w {};                                       // current link weights (cycles per unit output)
        std::array<std::array<float, numOps>, maxUnison> cgL {}, cgR {};   // carrier gains
        std::array<Decimator, 2> dec {};
        int note = 60, unison = 1;
        float velocity = 1.0f;
        bool started = false, pendingStart = false, pendingRelease = false, legato = false;
    };

    struct Targets
    {
        std::array<float, numOps> amp {};
        Matrix w {};
        std::array<std::array<float, numOps>, maxUnison> cgL {}, cgR {};
        std::array<std::array<float, numOps>, maxUnison> inc0 {}, inc1 {};
        std::array<float, maxUnison> fund0 {}, fund1 {};
        std::array<bool, numOps> on {}, sync {};
        std::array<int, numOps> wave {};
        int unison = 1;
    };

    void start (VoiceState&, const EngineVoiceParams&, bool retrigger) noexcept;
    void computeTargets (VoiceState&, const EngineVoiceParams&, int n, float pitch0, float pitch1, Targets&) noexcept;
    void renderBlock (VoiceState&, const EngineVoiceParams&, float* left, float* right, int n, float p0, float p1) noexcept;

    double sr = 48000.0;
    int os = 2, maxBlock = 32;
    std::vector<VoiceState> voices;
    std::vector<float> busL, busR, work;
    Targets tg;   // scratch (rendering is sequential)
};
} // namespace chroma
