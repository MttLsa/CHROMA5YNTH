#include "AdditiveSpectrum.h"
#include <algorithm>
#include <cmath>

namespace chroma::add
{
namespace
{
constexpr double pi = 3.14159265358979323846;

const std::array<FieldInfo, numFields> infos { {
    { "Ratio", 0.01f, 1024.0f, 1.0f }, { "Freq", 0.0f, 20000.0f, 0.0f }, { "Amp", 0.0f, 1.0f, 0.0f },
    { "Phase", 0.0f, 1.0f, 0.0f }, { "Detune", -50.0f, 50.0f, 0.0f }, { "Fine", -100.0f, 100.0f, 0.0f },
    { "Pan", -1.0f, 1.0f, 0.0f }, { "Polarity", -1.0f, 1.0f, 1.0f }, { "Mute", 0.0f, 1.0f, 0.0f },
    { "Solo", 0.0f, 1.0f, 0.0f } } };

// Gaussian formant bump in log-frequency (octaves).
double formant (double hz, double centre, double widthOct, double gain)
{
    const double d = std::log2 (hz / centre) / widthOct;
    return gain * std::exp (-0.5 * d * d);
}

double vowel (double hz, const double (&f)[3])
{
    return 0.03 + formant (hz, f[0], 0.35, 1.0) + formant (hz, f[1], 0.3, 0.6) + formant (hz, f[2], 0.25, 0.25);
}
} // namespace

const FieldInfo& fieldInfo (int field) noexcept { return infos[(size_t) std::clamp (field, 0, numFields - 1)]; }

void CustomSpectrum::set (int field, int k, float x) noexcept
{
    if (field < 0 || field >= numFields || k < 0 || k >= maxPartials || ! std::isfinite (x))
        return;
    const auto& fi = infos[(size_t) field];
    if (field == fPolarity) x = x < 0.0f ? -1.0f : 1.0f;
    if (field == fMute || field == fSolo) x = x > 0.5f ? 1.0f : 0.0f;
    v[(size_t) field][(size_t) k].store (std::clamp (x, fi.min, fi.max), std::memory_order_relaxed);
    ver.fetch_add (1, std::memory_order_release);
}

void makeShape (int shape, SpectrumData& d) noexcept
{
    for (int f = 0; f < numFields; ++f)
        d.v[(size_t) f].fill (infos[(size_t) f].def);
    for (int i = 0; i < maxPartials; ++i)
    {
        const int k = i + 1;
        const double kd = (double) k;
        double amp = 0.0, ratio = kd, phase = 0.0;
        switch (shape)
        {
            case Sine:     amp = k == 1 ? 1.0 : 0.0; break;
            case Custom:
            case Saw:      amp = 1.0 / kd; break;
            case Square:   amp = (k & 1) ? 1.0 / kd : 0.0; break;
            case Triangle: amp = (k & 1) ? ((k / 2) % 2 == 0 ? 1.0 : -1.0) / (kd * kd) : 0.0; break;
            case Pulse25:  amp = std::sin (pi * kd * 0.25) / (kd * std::sin (pi * 0.25)); break;
            case Flat:     amp = 0.25; phase = std::fmod (kd * kd / (2.0 * maxPartials), 1.0); break;   // low crest factor
            case Organ:
            {
                static constexpr int h[] = { 1, 2, 3, 4, 6, 8, 10, 12, 16 };
                static constexpr double a[] = { 1.0, 0.8, 0.6, 0.5, 0.35, 0.3, 0.15, 0.1, 0.08 };
                for (size_t j = 0; j < 9; ++j)
                    if (h[j] == k) amp = a[j];
                break;
            }
            case Hollow:   amp = (k & 1) ? 1.0 / kd : 0.12 / kd; break;
            case Bell:
            {
                static constexpr double r[] = { 0.5, 1.0, 1.2, 1.5, 2.0, 2.51, 2.66, 3.01, 4.1, 5.43, 6.8, 8.21 };
                ratio = k <= 12 ? r[k - 1] : 0.62 * std::pow (kd, 1.1);
                amp = (k == 2 ? 1.0 : 0.7) / std::pow (kd, 0.8) * (0.6 + 0.4 * std::abs (std::sin (kd * 2.3)));
                break;
            }
            case VocalA:   amp = vowel (130.0 * kd, { 800.0, 1150.0, 2900.0 }) / std::sqrt (kd); break;
            case VocalO:   amp = vowel (130.0 * kd, { 450.0, 800.0, 2830.0 }) / std::sqrt (kd); break;
            case Strings:  amp = std::pow (kd, -0.9) * (0.25 + 0.75 * std::abs (std::sin (pi * kd / 7.0))); break;
            case Glass:
            {
                const double m = (2.0 * kd + 1.0) / 3.0;
                ratio = m * m;
                amp = 1.0 / kd;
                break;
            }
            default: break;
        }
        d.at (fRatio, i) = (float) std::clamp (ratio, 0.01, 1024.0);
        d.at (fAmp, i) = (float) std::min (1.0, std::abs (amp));
        d.at (fPolarity, i) = amp < 0.0 ? -1.0f : 1.0f;
        d.at (fPhase, i) = (float) phase;
    }
}

const SpectrumData& shapeTable (int shape) noexcept
{
    static const auto tables = []
    {
        std::array<SpectrumData, numShapes> t {};
        for (int s = 0; s < numShapes; ++s)
            makeShape (s, t[(size_t) s]);
        return t;
    }();
    return tables[(size_t) std::clamp (shape, 0, numShapes - 1)];
}
} // namespace chroma::add
