#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace chroma
{
// FILTER LIBRARY (§2). Order matches `filter_models` in params/layer_engine.yaml.
enum class FilterModel : int
{
    LadderLP24,   // transistor ladder (Moog)
    DiodeLP,      // diode ladder (TB-303)
    SvfLP12, SvfBP12, SvfHP12, SvfNotch,   // 2-pole SVF (Oberheim SEM)
    OtaLP24,      // OTA cascade (Roland IR3109)
    CascadeLP24,  // CEM3320 / SSM2040
    SkLP12, SkHP12,   // Sallen-Key (Korg MS-20)
    BridgeLP12,   // Steiner-Parker
    ClassicLP24,  // ARP 4072
    DigLP12, DigLP24, DigLP36, DigLP48, DigHP12, DigHP24, DigBP, DigNotch,
    CombPlus, CombMinus, Formant,
    count
};

inline constexpr bool isAnalogFilter (FilterModel m) noexcept { return (int) m <= (int) FilterModel::ClassicLP24; }

// Stereo per-voice filter. Analog models: ZDF (TPT) N-pole core solved implicitly for the linear part,
// with the saturator inside the feedback loop -> bounded self-oscillation at any sample rate.
// Digital models: linear TPT SVF cascades, comb, formant. Audio thread: no allocation.
class Filter
{
public:
    void prepare (double sampleRate);           // allocates comb lines (not on the audio thread)
    void reset() noexcept;
    void setModel (FilterModel) noexcept;       // resets state when the model changes
    void setResonance (float r) noexcept { res = r; }
    void setDrive (float d) noexcept { drive = d; }
    FilterModel model() const noexcept { return mdl; }

    // In-place. Cutoff is interpolated exponentially from cutoffStart to cutoffEnd (Hz) across n samples.
    void process (float* left, float* right, int n, float cutoffStartHz, float cutoffEndHz) noexcept;

private:
    static constexpr int sub = 4;   // coefficient update period (samples)
    static constexpr int maxPoles = 4;

    void updateCoeffs (float cutoffHz) noexcept;
    void sanitize() noexcept;
    float processAnalog (float x, int ch) noexcept;
    float processDigital (float x, int ch) noexcept;
    float processComb (float x, int ch) noexcept;

    double sr = 48000.0;
    FilterModel mdl = FilterModel::LadderLP24;
    float res = 0.0f, drive = 0.0f;
    float lastCutoff = -1.0f, lastRes = -1.0f, lastDrive = -1.0f;
    bool configured = false;
    uint32_t noise = 0x1234567u;

    // ---- analog core: (I - gA) y = s + g b u,  u = x + k c.y,  u_nl = sat(u)
    int poles = 4, sat = 0;
    float A[maxPoles][maxPoles] {}, b[maxPoles] {}, c[maxPoles] {};
    float P[maxPoles][maxPoles] {}, yb[maxPoles] {};
    float k = 0.0f, G = 0.0f, satSlope = 1.0f, comp = 0.0f, kCrit = 4.0f, wScale = 1.0f;
    float inGain = 1.0f, outGain = 1.0f;
    float st[2][maxPoles] {};

    // ---- digital SVF sections
    struct Svf { float a1 = 0, a2 = 0, a3 = 0, k = 1.4142f, gain = 1.0f; };
    std::array<Svf, 4> svf {};
    int sections = 1;
    float ic[2][4][2] {};

    // ---- comb
    std::vector<float> line[2];
    int writePos = 0;
    float delaySamples = 100.0f, fb = 0.0f;
};
} // namespace chroma
