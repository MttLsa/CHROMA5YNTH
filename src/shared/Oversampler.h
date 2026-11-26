#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace chroma
{
// Linear-phase halfband FIR (Kaiser windowed sinc), designed once.
struct Halfband
{
    std::vector<float> taps;   // odd length, symmetric; odd offsets from centre are zero
    int length = 0;

    void design (int numTaps, double beta)
    {
        const auto i0 = [] (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 64; ++k) { term *= (x * 0.5 / k) * (x * 0.5 / k); sum += term; }
            return sum;
        };
        constexpr double pi = 3.14159265358979323846;
        length = numTaps | 1;
        const int c = (length - 1) / 2;
        std::vector<double> h ((size_t) length, 0.0);
        double total = 0.0;
        for (int i = 0; i < length; ++i)
        {
            const int n = i - c;
            if (n != 0 && n % 2 == 0) continue;               // halfband: zero at even offsets
            const double sinc = n == 0 ? 0.5 : std::sin (pi * n * 0.5) / (pi * n);
            const double r = (double) n / c;
            h[(size_t) i] = sinc * i0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0 (beta);
            total += h[(size_t) i];
        }
        taps.assign ((size_t) length, 0.0f);
        for (int i = 0; i < length; ++i)
            taps[(size_t) i] = (float) (h[(size_t) i] / total);
    }

    static const Halfband& longStage()  { static const Halfband h = make (95, 12.0); return h; }   // 2x -> 1x
    static const Halfband& shortStage() { static const Halfband h = make (23, 12.0); return h; }   // 8x -> 4x, 4x -> 2x

private:
    static Halfband make (int n, double b) { Halfband h; h.design (n, b); return h; }
};

// Per-channel decimation state for 2x/4x/8x -> 1x. The caller provides scratch memory.
class Decimator
{
public:
    static constexpr int maxHistory = 96;

    void reset() noexcept { for (auto& h : hist) h.fill (0.0f); }

    static constexpr int workSize (int maxInput) noexcept { return maxInput + maxHistory + maxInput / 2; }

    // in: n * factor samples; out: n samples. work: >= workSize (n * factor) floats.
    void process (const float* in, float* out, int n, int factor, float* work) noexcept
    {
        if (factor <= 1) { std::copy (in, in + n, out); return; }
        float* tmp = work + n * factor + maxHistory;   // intermediate stage output
        const float* src = in;
        int len = n * factor, stage = 0;
        for (int f = factor; f > 1; f >>= 1, ++stage)
        {
            float* dst = f == 2 ? out : tmp;
            runStage (f == 2 ? Halfband::longStage() : Halfband::shortStage(), hist[(size_t) stage], src, len, dst, work);
            src = dst;
            len /= 2;
        }
    }

private:
    // y[m] = sum_k h[k] x[2m + 1 - k + (L - 1)] over the concatenation history | input.
    static void runStage (const Halfband& hb, std::array<float, maxHistory>& h, const float* x, int len,
                          float* y, float* work) noexcept
    {
        const int L = hb.length, H = L - 1;
        float* buf = work;   // H history + len input, contiguous
        std::copy (h.begin(), h.begin() + H, buf);
        std::copy (x, x + len, buf + H);
        const float* t = hb.taps.data();
        const int c = H / 2;
        for (int m = 0; m < len / 2; ++m)
        {
            const float* p = buf + 2 * m + 1;   // window start
            float acc = t[c] * p[c];
            for (int k = (c & 1) ? 0 : 1; k < c; k += 2)
                acc += t[k] * (p[k] + p[H - k]);
            y[m] = acc;
        }
        std::copy (buf + len, buf + len + H, h.begin());
    }

    std::array<std::array<float, maxHistory>, 3> hist {};
};
} // namespace chroma
