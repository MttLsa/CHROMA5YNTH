#pragma once
#include "Wavetable.h"
#include "engines/IEngine.h"
#include "shared/Oversampler.h"
#include <array>
#include <complex>
#include <cstdint>
#include <vector>

namespace chroma
{
// ENGINE WAVETABLE (§3D): 3 oscillators reading band-limited mipmapped wavetables (linear between frames,
// 4-point Hermite within a frame), per-OSC unison 1-16, warp modes, spectral processing (freeze / blur / tilt / formant shift /
// harmonic stretch) through per-voice resynthesised tables, phase distortion, bit depth, SR reduction,
// granular playback; Sub and Noise. Internal oversampling (default 2x) + halfband decimation.
// Filter 1 + Filter 2 with Serial/Parallel routing are run by the Voice (dualFilter()).
class WavetableEngine final : public IEngine
{
public:
    enum Warp { WarpOff, BendPlus, BendMinus, BendBoth, Pwm, Sync, Mirror, Asym, Quantize, Fm, Am, Ring };
    static constexpr int numOsc = 3, maxOversampling = 8;
    static constexpr int dynLen = 1024, dynHarmonics = 256, dynUpdate = 64;   // spectral path (base-rate update period)

    WavetableEngine();
    void prepare (double sampleRate, int maxBlockSize, int maxVoices) override;
    void reset() override;
    void noteOn (int voice, const EngineNote&) override;
    void noteOff (int) override {}
    void kill (int voice) override;
    void setOversampling (int factor) override;
    bool dualFilter() const noexcept override { return true; }
    bool render (int voice, const EngineVoiceParams&, float* left, float* right, int n) noexcept override;

    // User tables (editor), owned by the synth / layer store. slots: numUserSlots entries, may be null.
    void setUserSlots (const wt::TableSlot* slots) noexcept { users = slots; }
    const wt::Table* tableFor (int choice) const noexcept;
    int oversampling() const noexcept { return os; }

    // Phase warp (public for tests). Returns the read phase; silent = true when PWM blanks the sample.
    static double warpPhase (int mode, double p, float amount, float mod, bool& silent) noexcept;

private:
    struct Dyn
    {
        std::array<float, dynLen + 4> a {}, b {};   // data() + 1 = table (wrap guards)
        std::array<float, wt::specSize> frozen {};
        float xf = 1.0f;
        int countdown = 0;
        bool valid = false, haveFrozen = false;
    };
    struct Osc
    {
        std::array<double, maxUnison> phase {};
        double scan = 0.0;
        float pos = 0.0f, gainL = 0.0f, gainR = 0.0f, width = 1.0f;
        bool havePos = false, active = false;
        double grainPhase = 0.0;
        float offA = 0.0f, offB = 0.0f;
        float holdL = 0.0f, holdR = 0.0f, holdRaw = 0.0f, holdCount = 0.0f;
    };
    struct VoiceState
    {
        std::array<Osc, numOsc> osc {};
        double subPhase = 0.0;
        float subGainL = 0.0f, subGainR = 0.0f;
        bool subActive = false, pendingStart = false;
        uint32_t rngL = 1, rngR = 2, seed = 1;
        std::array<float, 3> pink {};
        float noiseGainL = 0.0f, noiseGainR = 0.0f;
        std::array<Decimator, 2> dec {};
        int hold = 0;
    };
    struct OscSetup;

    bool renderBlock (int voice, VoiceState&, const EngineVoiceParams&, float* left, float* right, int n,
                      float pitch0, float pitch1) noexcept;
    void renderOsc (int voice, VoiceState&, int k, const EngineVoiceParams&, int n, float pitch0, float pitch1,
                    const float* mod, float* L, float* R, float* raw) noexcept;
    void updateDyn (Dyn&, const wt::Table&, float pos, const EngineVoiceParams&, int k, int harmonics) noexcept;
    void renderSub (VoiceState&, const EngineVoiceParams&, int N, float pitch0, float pitch1) noexcept;

    double sr = 48000.0;
    int os = 2, maxBlock = 32, maxN = 256;
    std::vector<VoiceState> voices;
    std::vector<Dyn> dyn;                         // voices * numOsc
    std::vector<float> prevRaw;                   // voices * numOsc * maxN: last block of each OSC (warp source)
    const wt::TableSlot* users = nullptr;

    // scratch
    std::array<std::vector<float>, numOsc> oscL, oscR, oscRaw;
    std::vector<float> subBuf, busL, busR, work, fftWork;
    std::vector<std::complex<float>> specA, specB;
    std::vector<float> mags, tmpMags;
};
} // namespace chroma
