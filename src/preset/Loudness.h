#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace chroma::loudness
{
// ITU-R BS.1770-4 / EBU R128 measurements used by tools/presetnorm (§6). Offline, not real-time safe.

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// K-weighting (pre-filter high shelf + RLB high-pass), coefficients derived for any sample rate.
inline void kWeighting (double fs, Biquad& shelf, Biquad& hp)
{
    constexpr double pi = 3.14159265358979323846;
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (pi * f0 / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
        shelf.b1 = 2.0 * (K * K - Vh) / a0;
        shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
        shelf.a1 = 2.0 * (K * K - 1.0) / a0;
        shelf.a2 = (1.0 - K / Q + K * K) / a0;
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (pi * f0 / fs), a0 = 1.0 + K / Q + K * K;
        hp.b0 = 1.0; hp.b1 = -2.0; hp.b2 = 1.0;
        hp.a1 = 2.0 * (K * K - 1.0) / a0;
        hp.a2 = (1.0 - K / Q + K * K) / a0;
    }
}

inline double powerToLufs (double meanSquare) { return meanSquare > 1e-20 ? -0.691 + 10.0 * std::log10 (meanSquare) : -200.0; }

// Short-term loudness (3 s window, 100 ms hop) of each full window; returns the maximum in LUFS.
// Channels are weighted 1.0 (L/R). Shorter signals: one window over the whole signal.
inline double shortTermMax (const std::vector<std::vector<float>>& ch, double fs, double windowSec = 3.0, double hopSec = 0.1)
{
    if (ch.empty() || ch[0].empty()) return -200.0;
    const size_t n = ch[0].size();
    std::vector<double> sq (n, 0.0);
    for (const auto& x : ch)
    {
        Biquad s, h;
        kWeighting (fs, s, h);
        for (size_t i = 0; i < n; ++i)
        {
            const double y = h.process (s.process ((double) x[i]));
            sq[i] += y * y;
        }
    }
    std::vector<double> prefix (n + 1, 0.0);
    for (size_t i = 0; i < n; ++i) prefix[i + 1] = prefix[i] + sq[i];
    const size_t win = std::min (n, (size_t) std::lround (windowSec * fs));
    const size_t hop = std::max<size_t> (1, (size_t) std::lround (hopSec * fs));
    double best = -200.0;
    for (size_t end = win; end <= n; end += hop)
        best = std::max (best, powerToLufs ((prefix[end] - prefix[end - win]) / (double) win));
    best = std::max (best, powerToLufs ((prefix[n] - prefix[n - win]) / (double) win));
    return best;
}

// True peak (dBTP): 4× oversampling (BS.1770-4 Annex 2 style), windowed-sinc polyphase
// interpolator, 24 taps per phase; max over channels.
inline double truePeakDb (const std::vector<std::vector<float>>& ch)
{
    constexpr int os = 4, taps = 24, half = taps / 2;
    constexpr double pi = 3.14159265358979323846;
    static const auto phases = []
    {
        std::vector<std::vector<double>> h ((size_t) os, std::vector<double> ((size_t) taps));
        for (int p = 1; p < os; ++p)
            for (int k = 0; k < taps; ++k)
            {
                const double t = (double) (k - half + 1) - (double) p / os;   // tap distance in input samples
                const double r = t / (double) half;
                const double w = std::abs (r) >= 1.0 ? 0.0 : std::cos (0.5 * pi * r) * std::cos (0.5 * pi * r);
                h[(size_t) p][(size_t) k] = std::sin (pi * t) / (pi * t) * w;
            }
        return h;
    }();
    double peak = 0.0;
    for (const auto& x : ch)
    {
        const int n = (int) x.size();
        for (int i = 0; i < n; ++i)
        {
            peak = std::max (peak, (double) std::abs (x[(size_t) i]));
            if (i < half || i + half >= n) continue;
            for (int p = 1; p < os; ++p)
            {
                // y(i - 1 + p/os + ...) : taps cover x[i - half + 1 .. i + half]
                const auto& h = phases[(size_t) p];
                double acc = 0.0;
                for (int k = 0; k < taps; ++k)
                    acc += (double) x[(size_t) (i - half + 1 + k)] * h[(size_t) k];
                peak = std::max (peak, std::abs (acc));
            }
        }
    }
    return peak > 1e-12 ? 20.0 * std::log10 (peak) : -240.0;
}
} // namespace chroma::loudness
