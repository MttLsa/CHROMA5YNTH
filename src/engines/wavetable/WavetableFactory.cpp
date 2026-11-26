// Factory wavetables (§3D): all generated procedurally here, no third-party material.
// Each table is defined either in the time domain (evaluated at 8192 points, then band-limited to 512
// harmonics) or directly as a harmonic spectrum. Frames are peak-normalised individually.
#include "Wavetable.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <functional>
#include <mutex>

namespace chroma::wt
{
namespace
{
constexpr double pi = 3.14159265358979323846, twoPi = 2.0 * pi;
constexpr int framesPerTable = 32;
constexpr int evalSize = 8192, evalOrder = 13;

using Spectrum = std::array<std::complex<float>, specSize>;
using TimeFn = std::function<double (double t, double m)>;          // t in [0,1), m = morph 0..1
using SpecFn = std::function<void (double m, Spectrum&)>;

inline double frac (double x) noexcept { return x - std::floor (x); }
inline double saw (double t) noexcept { return 2.0 * frac (t) - 1.0; }
inline double square (double t) noexcept { return frac (t) < 0.5 ? 1.0 : -1.0; }
inline double tri (double t) noexcept { const double p = frac (t + 0.25); return p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p; }
inline double sine (double t) noexcept { return std::sin (twoPi * t); }
inline double pulse (double t, double w) noexcept { return frac (t) < w ? 1.0 : -1.0; }
inline double lerp (double a, double b, double x) noexcept { return a + (b - a) * x; }
inline double fold (double x) noexcept { return std::asin (std::sin (x * pi * 0.5)) * 2.0 / pi; }
inline double asymPhase (double t, double knee) noexcept
{
    return t < knee ? 0.5 * t / knee : 0.5 + 0.5 * (t - knee) / (1.0 - knee);
}
inline void addSin (Spectrum& s, int k, double a) noexcept { if (k > 0 && k < specSize) s[(size_t) k] += std::complex<float> (0.0f, (float) -a); }
inline void addPolar (Spectrum& s, int k, double a, double ph) noexcept
{
    if (k > 0 && k < specSize) s[(size_t) k] += std::polar ((float) a, (float) ph);
}
inline double gauss (double x, double w) noexcept { return std::exp (-(x * x) / (2.0 * w * w)); }

// Formant envelope: sum of resonances (Hz) for a source at fundamental f0.
double formantGain (double hz, const double* f, const double* g, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double bw = 60.0 + 0.06 * f[i];
        const double x = (hz - f[i]) / bw;
        s += g[i] / (1.0 + x * x);
    }
    return s;
}

void vowelSpectrum (double m, Spectrum& s, double tilt, const int* order, int count)
{
    // F1..F3 (Hz) of generic vowels A E I O U
    static const double F[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
    static const double G[3] = { 1.0, 0.5, 0.25 };
    const double x = m * (count - 1);
    const int i0 = std::min ((int) x, count - 2);
    const double a = x - i0;
    double f[3];
    for (int j = 0; j < 3; ++j)
        f[j] = lerp (F[order[i0]][j], F[order[i0 + 1]][j], a);
    constexpr double f0 = 110.0;
    for (int k = 1; k * f0 < 6000.0 && k < specSize; ++k)
        addSin (s, k, std::pow ((double) k, -tilt) * (0.05 + formantGain (k * f0, f, G, 3)));
}

uint32_t hashRand (uint32_t x) noexcept
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
double rnd (uint32_t a, uint32_t b) noexcept { return (double) (hashRand (a * 7919u + b * 104729u + 17u) >> 8) / 16777216.0; }

struct Def
{
    const char* name;
    TimeFn time;
    SpecFn spec;
};

std::vector<Def> definitions()
{
    std::vector<Def> d;
    const auto T = [&d] (const char* n, TimeFn f) { d.push_back ({ n, std::move (f), nullptr }); };
    const auto S = [&d] (const char* n, SpecFn f) { d.push_back ({ n, nullptr, std::move (f) }); };

    T ("Basic Shapes", [] (double t, double m)
    {
        const double x = m * 4.0;
        const int i = std::min ((int) x, 3);
        const auto shape = [t] (int k)
        {
            switch (k) { case 0: return sine (t); case 1: return tri (t); case 2: return saw (t); case 3: return square (t); default: return pulse (t, 0.1); }
        };
        return lerp (shape (i), shape (i + 1), x - i);
    });
    S ("Sine Harmonics", [] (double m, Spectrum& s)
    {
        const double n = 1.0 + m * 63.0;
        for (int k = 1; k <= (int) std::ceil (n); ++k)
            addSin (s, k, std::min (1.0, n - k + 1.0) / k);
    });
    T ("PWM", [] (double t, double m) { return pulse (t, lerp (0.5, 0.02, m)); });
    T ("Saw Sync", [] (double t, double m) { return saw (t * (1.0 + 7.0 * m)); });
    T ("Square Sync", [] (double t, double m) { return square (t * (1.0 + 7.0 * m)); });
    T ("Sine Sync", [] (double t, double m) { return sine (frac (t * (1.0 + 7.0 * m))); });
    S ("Vowels", [] (double m, Spectrum& s) { static const int o[5] = { 0, 1, 2, 3, 4 }; vowelSpectrum (m, s, 0.6, o, 5); });
    S ("Formant Sweep", [] (double m, Spectrum& s)
    {
        const double h = 2.0 + 60.0 * m;
        for (int k = 1; k < 200; ++k)
            addSin (s, k, (0.15 + 3.0 * gauss (k - h, 1.5 + h * 0.05)) / k);
    });
    S ("Odd Harmonics", [] (double m, Spectrum& s)
    {
        addSin (s, 1, 1.0);
        const double n = m * 32.0;
        for (int j = 1; j <= (int) std::ceil (n); ++j)
            addSin (s, 2 * j + 1, std::min (1.0, n - j + 1.0) / (2 * j + 1));
    });
    S ("Even Harmonics", [] (double m, Spectrum& s)
    {
        addSin (s, 1, 1.0);
        const double n = m * 32.0;
        for (int j = 1; j <= (int) std::ceil (n); ++j)
            addSin (s, 2 * j, std::min (1.0, n - j + 1.0) / (2 * j));
    });
    S ("Harmonic Sweep", [] (double m, Spectrum& s)
    {
        addSin (s, 1, 1.0);
        const double h = 2.0 + 46.0 * m;
        for (int k = 2; k < 64; ++k) addSin (s, k, 0.8 * gauss (k - h, 0.7));
    });
    S ("Spectral Comb", [] (double m, Spectrum& s)
    {
        for (int k = 1; k < 256; ++k)
            addSin (s, k, (0.5 + 0.5 * std::cos (twoPi * (k / 8.0 + m))) / k);
    });
    S ("Bell", [] (double m, Spectrum& s)
    {
        static const int h[] = { 1, 2, 5, 6, 9, 11, 13, 17, 23 };
        for (int i = 0; i < 9; ++i)
            addPolar (s, h[i], std::pow (0.75, i) * (i == 0 ? 1.0 : lerp (0.2, 1.6, m) * (1.0 + 0.3 * std::sin (i + m * 6.0))), i * 1.3);
    });
    S ("Drawbars", [] (double m, Spectrum& s)
    {
        static const int h[9] = { 1, 2, 3, 4, 6, 8, 10, 12, 16 };
        static const double reg[4][9] = { { 8, 8, 8, 0, 0, 0, 0, 0, 0 }, { 8, 6, 8, 4, 0, 0, 0, 0, 3 },
                                          { 6, 8, 8, 6, 4, 2, 0, 0, 0 }, { 8, 8, 8, 8, 8, 8, 8, 8, 8 } };
        const double x = m * 3.0;
        const int i = std::min ((int) x, 2);
        for (int j = 0; j < 9; ++j)
            addSin (s, h[j], lerp (reg[i][j], reg[i + 1][j], x - i) / 8.0);
    });
    T ("FM 1:1", [] (double t, double m) { return std::sin (twoPi * t + 8.0 * m * std::sin (twoPi * t)); });
    T ("FM 2:1", [] (double t, double m) { return std::sin (twoPi * t + 6.0 * m * std::sin (2.0 * twoPi * t)); });
    T ("FM 3:2", [] (double t, double m) { return std::sin (2.0 * twoPi * t + 5.0 * m * std::sin (3.0 * twoPi * t)); });
    T ("FM Feedback", nullptr);   // computed sequentially below
    T ("PD Saw", [] (double t, double m) { return -std::cos (twoPi * asymPhase (t, lerp (0.5, 0.01, m))); });
    T ("PD Reso", [] (double t, double m) { return (1.0 - t) * std::cos (twoPi * t * (1.0 + 15.0 * m)) * 2.0 - (1.0 - t); });
    T ("Wavefolder", [] (double t, double m) { return fold (sine (t) * (1.0 + 7.0 * m)); });
    T ("Triangle Fold", [] (double t, double m) { return fold (tri (t) * (1.0 + 7.0 * m)); });
    T ("Drive", [] (double t, double m) { const double g = 1.0 + 30.0 * m * m; return std::tanh (g * sine (t)) / std::tanh (g); });
    T ("Chebyshev", [] (double t, double m)
    {
        const double x = (0.05 + 0.95 * m) * std::cos (twoPi * t);
        return 64.0 * std::pow (x, 7) - 112.0 * std::pow (x, 5) + 56.0 * std::pow (x, 3) - 7.0 * x;   // T7
    });
    T ("Bitcrush", [] (double t, double m)
    {
        const double levels = std::pow (2.0, lerp (7.0, 1.0, m));
        return std::round (sine (t) * levels) / levels;
    });
    T ("Saw Stack", [] (double t, double m)
    {
        double y = saw (t);
        for (int j = 2; j <= 5; ++j)
            y += std::clamp (m * 4.0 - (j - 2), 0.0, 1.0) * saw (t * j) * 0.7;
        return y;
    });
    S ("Spectral Tilt", [] (double m, Spectrum& s)
    {
        const double e = lerp (-2.0, 0.5, m);
        for (int k = 1; k <= 128; ++k) addSin (s, k, std::pow ((double) k, e));
    });
    S ("Band Noise", [] (double m, Spectrum& s)
    {
        const double c = 1.0 + 7.0 * m;   // centre octave
        for (int k = 1; k < specSize; ++k)
            addPolar (s, k, gauss (std::log2 ((double) k) - c, 0.45) * (0.5 + 0.5 * rnd ((uint32_t) k, 1u)), twoPi * rnd ((uint32_t) k, 2u));
    });
    S ("Random Harmonics", [] (double m, Spectrum& s)
    {
        const double x = m * 3.0;
        const int i = std::min ((int) x, 2);
        for (int k = 1; k <= 64; ++k)
        {
            const double a0 = rnd ((uint32_t) k, (uint32_t) (10 + i)), a1 = rnd ((uint32_t) k, (uint32_t) (11 + i));
            addSin (s, k, lerp (a0, a1, x - i) / std::sqrt ((double) k));
        }
    });
    T ("Steps", [] (double t, double m)
    {
        const double n = 2.0 + std::round (m * 30.0);
        return 2.0 * std::floor (t * n) / (n - 1.0) - 1.0;
    });
    T ("Windowed Sine", [] (double t, double m) { return std::sin (twoPi * t * (1.0 + 15.0 * m)) * 0.5 * (1.0 - std::cos (twoPi * t)); });
    S ("Sine Pairs", [] (double m, Spectrum& s)
    {
        addSin (s, 1, 1.0);
        const double h = 2.0 + 14.0 * m;
        const int k = (int) h;
        addSin (s, k, 0.7 * (1.0 - (h - k)));
        addSin (s, k + 1, 0.7 * (h - k));
    });
    S ("Hollow", [] (double m, Spectrum& s)
    {
        const double h = 3.0 + 20.0 * m;
        for (int k = 1; k < 256; k += 2) addSin (s, k, (1.0 - gauss (k - h, 2.0)) / k);
    });
    S ("Choir", [] (double m, Spectrum& s) { static const int o[4] = { 0, 3, 4, 1 }; vowelSpectrum (m, s, 0.9, o, 4); });
    S ("Brass", [] (double m, Spectrum& s)
    {
        const double a = lerp (2.6, 0.9, m);
        static const double f[1] = { 1200.0 }, g[1] = { 1.0 };
        for (int k = 1; k < 128; ++k) addSin (s, k, std::pow ((double) k, -a) * (0.6 + formantGain (k * 220.0, f, g, 1)));
    });
    S ("Strings", [] (double m, Spectrum& s)
    {
        for (int k = 1; k < 200; ++k)
        {
            const double body = 0.6 + 0.8 * rnd ((uint32_t) k, 20u);
            addSin (s, k, body * std::exp (-k / lerp (6.0, 60.0, m)) / k);
        }
    });
    S ("Reed", [] (double m, Spectrum& s)
    {
        const double h = 5.0 + 10.0 * m;
        for (int k = 1; k < 128; ++k)
            addSin (s, k, ((k & 1) ? 1.0 : 0.25) * (1.0 + 2.0 * gauss (k - h, 2.0)) / k);
    });
    S ("Metallic", [] (double m, Spectrum& s)
    {
        static const int h[] = { 7, 11, 13, 17, 19, 23, 29, 31 };
        addSin (s, 1, 0.5);
        for (int i = 0; i < 8; ++i)
        {
            const int k = (int) std::round (h[i] * (1.0 + 2.0 * m));
            addPolar (s, k, 0.6 / (1.0 + 0.3 * i), i * 2.1);
            addPolar (s, k + 1, 0.3 / (1.0 + 0.3 * i), i * 0.7);
        }
    });
    S ("Glass", [] (double m, Spectrum& s)
    {
        for (int j = 1; j <= 8; ++j)
            addSin (s, j * j, (j == 1 ? 1.0 : 0.8 * std::pow (lerp (0.3, 0.95, m), j - 1)));
    });
    T ("Growl", [] (double t, double m) { return fold ((1.0 + 3.0 * m) * std::sin (twoPi * t + 3.0 * m * std::sin (2.0 * twoPi * t))); });
    T ("Pulse Train", [] (double t, double m) { return 2.0 * gauss (t - 0.5, lerp (0.25, 0.006, m)) - 1.0; });
    T ("Curved Saw", [] (double t, double m)
    {
        const double k = lerp (-8.0, 8.0, m);
        if (std::fabs (k) < 1.0e-6) return saw (t);
        return 2.0 * (std::exp (k * t) - 1.0) / (std::exp (k) - 1.0) - 1.0;
    });
    T ("Skewed Sine", [] (double t, double m) { return std::sin (twoPi * asymPhase (t, lerp (0.5, 0.02, m))); });
    T ("Rectifier", [] (double t, double m)
    {
        const double s = sine (t), half = std::max (s, 0.0) * 2.0 - 0.6366, full = std::fabs (s) * 2.0 - 1.2732;
        return m < 0.5 ? lerp (s, half, 2.0 * m) : lerp (half, full, 2.0 * m - 1.0);
    });
    S ("Octave Bands", [] (double m, Spectrum& s)
    {
        const double c = m * 8.0;
        addSin (s, 1, 0.5);
        for (int k = 2; k < specSize; ++k) addSin (s, k, gauss (std::log2 ((double) k) - c, 0.35) * 2.0 / std::sqrt ((double) k));
    });
    T ("Phase Shift Saw", [] (double t, double m) { return saw (t) + saw (t + 0.5 * m); });
    return d;
}

// One raw (512-harmonic band-limited) frame of frameSize samples.
void renderFrame (const Def& def, double m, float* out, std::vector<float>& work)
{
    Spectrum s {};
    if (def.spec)
    {
        def.spec (m, s);
    }
    else
    {
        work.assign ((size_t) evalSize * 2, 0.0f);
        if (def.time)
        {
            for (int i = 0; i < evalSize; ++i)
                work[(size_t) i] = (float) def.time ((double) i / evalSize, m);
        }
        else   // FM Feedback: y = sin(phi + beta * y[n-1]), two cycles to settle, keep the second
        {
            const double beta = 1.5 * m;
            double y = 0.0;
            for (int pass = 0; pass < 2; ++pass)
                for (int i = 0; i < evalSize; ++i)
                {
                    const double yn = std::sin (twoPi * i / evalSize + beta * y);
                    y = 0.5 * (y + yn);   // two-sample average (stable feedback)
                    work[(size_t) i] = (float) yn;
                }
        }
        static juce::dsp::FFT f (evalOrder);
        f.performRealOnlyForwardTransform (work.data());
        const float sc = 2.0f / (float) evalSize;
        for (int k = 1; k < specSize; ++k)
            s[(size_t) k] = { work[(size_t) (2 * k)] * sc, work[(size_t) (2 * k + 1)] * sc };
    }
    std::vector<float> w ((size_t) frameSize * 2 + 8);
    std::vector<float> tmp ((size_t) frameSize + 4);
    synthesize (s.data(), maxHarmonics, tmp.data() + 1, frameSize, w.data());
    float peak = 0.0f;
    for (int i = 0; i < frameSize; ++i) peak = std::max (peak, std::fabs (tmp[(size_t) i + 1]));
    const float g = peak > 1.0e-6f ? 1.0f / peak : 0.0f;
    for (int i = 0; i < frameSize; ++i) out[i] = tmp[(size_t) i + 1] * g;
}

const std::vector<Def>& defs()
{
    static const std::vector<Def> d = definitions();
    return d;
}
} // namespace

int numFactoryTables() noexcept { return (int) defs().size(); }

const char* factoryName (int index) noexcept
{
    const auto& d = defs();
    return index >= 0 && index < (int) d.size() ? d[(size_t) index].name : "";
}

std::vector<float> factoryFrames (int index, int& numFrames)
{
    const auto& d = defs();
    index = std::clamp (index, 0, (int) d.size() - 1);
    numFrames = framesPerTable;
    std::vector<float> frames ((size_t) framesPerTable * frameSize);
    std::vector<float> work;
    for (int f = 0; f < framesPerTable; ++f)
        renderFrame (d[(size_t) index], (double) f / (framesPerTable - 1), frames.data() + (size_t) f * frameSize, work);
    return frames;
}

const FactoryBank& factory()
{
    static const FactoryBank bank = []
    {
        FactoryBank b;
        const int n = numFactoryTables();
        b.tables.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            int nf = 0;
            const auto frames = factoryFrames (i, nf);
            b.tables[(size_t) i] = buildTable (frames.data(), nf, factoryName (i), true);
        }
        std::vector<float> sub ((size_t) 4 * frameSize);
        Def shapes { "Sub", [] (double t, double m)
        {
            const int k = (int) std::lround (m * 3.0);
            return k == 0 ? sine (t) : k == 1 ? tri (t) : k == 2 ? saw (t) : square (t);
        }, nullptr };
        std::vector<float> work;
        for (int f = 0; f < 4; ++f)
            renderFrame (shapes, f / 3.0, sub.data() + (size_t) f * frameSize, work);
        b.sub = buildTable (sub.data(), 4, "Sub", false);
        return b;
    }();
    return bank;
}
} // namespace chroma::wt
