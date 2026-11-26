#include "Filter.h"
#include <algorithm>
#include <cmath>

#if defined (__GNUC__)
 #pragma GCC diagnostic ignored "-Wswitch-enum"   // model switches use default on purpose
#endif

namespace chroma
{
namespace
{
constexpr float pi = 3.14159265358979f;

constexpr float tanhA (float x) noexcept   // Padé tanh, exact limits at ±3
{
    x = std::clamp (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

inline float saturate (int type, float x) noexcept
{
    switch (type)
    {
        case 1: return x / std::sqrt (1.0f + x * x);                         // OTA: soft algebraic
        case 2: { constexpr float bias = 0.3f;                               // asymmetric (even harmonics)
                  constexpr float off = tanhA (bias);
                  return tanhA (x + bias) - off; }
        case 3: return x / std::sqrt (std::sqrt (1.0f + x * x * x * x));    // diode clipper: harder knee
        default: return tanhA (x);                                           // transistor / diode ladder
    }
}

struct AnalogDesc { int poles, sat; float comp, kCrit, wScale; int topology; };   // 0 ladder, 1 diode, 2 svf, 3 sallen-key
inline AnalogDesc analogDesc (FilterModel m) noexcept
{
    switch (m)
    {
        case FilterModel::DiodeLP:     return { 4, 0, 0.30f, 8.6177686f, 1.0f / 1.0552897f, 1 };
        case FilterModel::SvfLP12:
        case FilterModel::SvfBP12:
        case FilterModel::SvfHP12:
        case FilterModel::SvfNotch:    return { 2, 1, 0.0f, 1.0f, 1.0f, 2 };
        case FilterModel::OtaLP24:     return { 4, 1, 0.50f, 4.0f, 1.0f, 0 };
        case FilterModel::CascadeLP24: return { 4, 2, 1.00f, 4.0f, 1.0f, 0 };
        case FilterModel::SkLP12:
        case FilterModel::SkHP12:      return { 2, 3, 0.0f, 2.0f, 1.0f, 3 };
        case FilterModel::BridgeLP12:  return { 2, 2, 0.20f, 2.0f, 1.0f, 3 };
        case FilterModel::ClassicLP24: return { 4, 3, 0.35f, 4.0f, 1.0f, 0 };
        default:                       return { 4, 0, 0.0f, 4.0f, 1.0f, 0 };
    }
}

// Butterworth section Qs for 2/4/6/8-pole cascades.
constexpr float bwQ[4][4] = { { 0.70711f, 0, 0, 0 },
                              { 0.54120f, 1.30656f, 0, 0 },
                              { 0.51764f, 0.70711f, 1.93185f, 0 },
                              { 0.50980f, 0.60134f, 0.89998f, 2.56292f } };

constexpr float formants[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 },
                                   { 570, 840, 2410 }, { 300, 870, 2240 } };   // A E I O U
constexpr float formantGain[3] = { 1.0f, 0.5f, 0.25f };
} // namespace

void Filter::prepare (double sampleRate)
{
    sr = sampleRate;
    const auto len = (size_t) (sampleRate / 20.0) + 8;
    for (auto& l : line) l.assign (len, 0.0f);
    configured = false;
    setModel (mdl);
}

void Filter::reset() noexcept
{
    for (auto& s : st) for (auto& v : s) v = 0.0f;
    for (auto& a : ic) for (auto& s : a) for (auto& v : s) v = 0.0f;
    if (mdl == FilterModel::CombPlus || mdl == FilterModel::CombMinus)
        for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f);
    writePos = 0;
}

void Filter::setModel (FilterModel m) noexcept
{
    if (m == mdl && configured)
        return;
    mdl = m;
    configured = true;
    lastCutoff = -1.0f;
    reset();

    for (auto& r : A) for (auto& v : r) v = 0.0f;
    for (auto& v : b) v = 0.0f;
    for (auto& v : c) v = 0.0f;
    if (! isAnalogFilter (m))
        return;

    const auto d = analogDesc (m);
    poles = d.poles; sat = d.sat; comp = d.comp; kCrit = d.kCrit; wScale = d.wScale;
    satSlope = (saturate (sat, 1.0e-3f) - saturate (sat, -1.0e-3f)) / 2.0e-3f;   // small-signal loop gain
    b[0] = 1.0f;
    switch (d.topology)
    {
        case 0:   // 4 cascaded one-poles, global negative feedback from the last stage
            for (int i = 0; i < 4; ++i) { A[i][i] = -1.0f; if (i > 0) A[i][i - 1] = 1.0f; }
            c[3] = -1.0f;
            break;
        case 1:   // diode ladder: each stage is loaded by the next one (coupling 0.5, tridiagonal)
            for (int i = 0; i < 4; ++i)
            {
                A[i][i] = -1.0f;
                if (i > 0) A[i][i - 1] = 1.0f;
                if (i < 3) { A[i][i] -= 0.5f; A[i][i + 1] = 0.5f; }
            }
            c[3] = -1.0f;
            break;
        case 2:   // SVF: own path (processAnalog), state in ic[][0]
            break;
        default:  // Sallen-Key: positive feedback k (y0 - y1)
            A[0][0] = -1.0f; A[1][0] = 1.0f; A[1][1] = -1.0f;
            c[0] = 1.0f; c[1] = -1.0f;
            break;
    }
}

void Filter::updateCoeffs (float fc) noexcept
{
    const float nyq = 0.45f * (float) sr;
    const float driveGain = std::exp2 (drive * 4.0f);   // 0..24 dB
    const float r = std::clamp (res, 0.0f, 1.0f);

    if (isAnalogFilter (mdl))
    {
        const float g = std::tan (pi * std::min (fc * wScale, nyq) / (float) sr);
        const bool svfTopo = mdl >= FilterModel::SvfLP12 && mdl <= FilterModel::SvfNotch;
        inGain = driveGain * (1.0f + comp * k);
        outGain = 1.0f / std::sqrt (driveGain);
        if (svfTopo)
        {
            // TPT SVF, damping 2R < 0 at max: self-oscillation bounded by the saturated BP integrator
            auto& f = svf[0];
            f.k = 2.0f * (1.0f - 1.02f * r);
            f.a1 = 1.0f / (1.0f + g * (g + f.k));
            f.a2 = g * f.a1;
            f.a3 = g * f.a2;
            inGain = driveGain;
            return;
        }
        k = kCrit * 1.03f * r / satSlope;

        // P = (I - gA)^-1 (Gauss-Jordan; the matrix is diagonally dominant, no pivoting needed)
        const int N = poles;
        float M[maxPoles][maxPoles];
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j)
            {
                M[i][j] = (i == j ? 1.0f : 0.0f) - g * A[i][j];
                P[i][j] = i == j ? 1.0f : 0.0f;
            }
        for (int col = 0; col < N; ++col)
        {
            const float inv = 1.0f / M[col][col];
            for (int j = 0; j < N; ++j) { M[col][j] *= inv; P[col][j] *= inv; }
            for (int row = 0; row < N; ++row)
            {
                if (row == col) continue;
                const float f = M[row][col];
                if (f == 0.0f) continue;
                for (int j = 0; j < N; ++j) { M[row][j] -= f * M[col][j]; P[row][j] -= f * P[col][j]; }
            }
        }
        G = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            yb[i] = g * P[i][0];   // b = e0
            G += c[i] * yb[i];
        }
        inGain = driveGain * (1.0f + comp * k);
        return;
    }

    inGain = driveGain;
    outGain = 1.0f / std::sqrt (driveGain);

    if (mdl == FilterModel::CombPlus || mdl == FilterModel::CombMinus)
    {
        const float maxD = (float) line[0].size() - 4.0f;
        delaySamples = std::clamp ((float) sr / std::max (fc, 20.0f), 2.0f, maxD);
        const float amt = 0.3f + 0.68f * r;
        fb = mdl == FilterModel::CombPlus ? amt : -amt;
        return;
    }

    auto setSvf = [this] (Svf& s, float f, float q)
    {
        const float g = std::tan (pi * std::min (f, 0.45f * (float) sr) / (float) sr);
        s.k = 1.0f / q;
        s.a1 = 1.0f / (1.0f + g * (g + s.k));
        s.a2 = g * s.a1;
        s.a3 = g * s.a2;
    };
    auto resQ = [r] (float qb, float qmax) { return qb * std::pow (qmax / qb, r); };

    switch (mdl)
    {
        case FilterModel::DigLP12: case FilterModel::DigLP24: case FilterModel::DigLP36: case FilterModel::DigLP48:
        case FilterModel::DigHP12: case FilterModel::DigHP24:
        {
            int order = 1;
            if (mdl == FilterModel::DigLP24 || mdl == FilterModel::DigHP24) order = 2;
            else if (mdl == FilterModel::DigLP36) order = 3;
            else if (mdl == FilterModel::DigLP48) order = 4;
            sections = order;
            for (int i = 0; i < order; ++i)
            {
                const float qb = bwQ[order - 1][i];
                setSvf (svf[(size_t) i], fc, i == order - 1 ? resQ (qb, 30.0f) : qb);
            }
            break;
        }
        case FilterModel::DigBP:    sections = 1; setSvf (svf[0], fc, resQ (0.5f, 25.0f)); break;
        case FilterModel::DigNotch: sections = 1; setSvf (svf[0], fc, resQ (0.4f, 8.0f)); break;
        case FilterModel::Formant:
        {
            sections = 3;
            const float pos = std::clamp (std::log2 (fc / 20.0f) / std::log2 (1000.0f), 0.0f, 1.0f) * 4.0f;
            const int v0 = std::min ((int) pos, 3);
            const float t = pos - (float) v0;
            for (int i = 0; i < 3; ++i)
            {
                const float f = formants[v0][i] * std::pow (formants[v0 + 1][i] / formants[v0][i], t);
                setSvf (svf[(size_t) i], f, resQ (5.0f, 20.0f));
                svf[(size_t) i].gain = formantGain[i];
            }
            break;
        }
        default: break;
    }
}

float Filter::processAnalog (float x, int ch) noexcept
{
    noise = noise * 1664525u + 1013904223u;
    const float dither = (float) (int32_t) noise * 4.6566e-16f;   // ~ -126 dBFS: starts self-oscillation

    if (mdl >= FilterModel::SvfLP12 && mdl <= FilterModel::SvfNotch)
    {
        x = 2.0f * saturate (sat, 0.5f * x * inGain) + dither;
        const auto& f = svf[0];
        float* s = ic[ch][0];
        const float v3 = x - s[1];
        const float v1 = f.a1 * s[0] + f.a2 * v3;
        const float v2 = s[1] + f.a2 * s[0] + f.a3 * v3;
        s[0] = 1.5f * saturate (sat, (2.0f * v1 - s[0]) * (1.0f / 1.5f));
        s[1] = 2.0f * v2 - s[1];
        float out;
        switch (mdl)
        {
            case FilterModel::SvfBP12:  out = v1; break;
            case FilterModel::SvfHP12:  out = x - f.k * v1 - v2; break;
            case FilterModel::SvfNotch: out = x - f.k * v1; break;
            default:                    out = v2; break;
        }
        return out * outGain;
    }

    x = x * inGain + dither;
    auto& s = st[ch];
    const int N = poles;
    float ys[maxPoles];
    float S = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        float a = 0.0f;
        for (int j = 0; j < N; ++j) a += P[i][j] * s[j];
        ys[i] = a;
        S += c[i] * a;
    }
    const float u = (x + k * S) / (1.0f - k * G);
    const float un = saturate (sat, u);
    float y[maxPoles];
    for (int i = 0; i < N; ++i)
    {
        y[i] = yb[i] * un + ys[i];
        s[i] = 2.0f * y[i] - s[i];
    }
    float out;
    switch (mdl)
    {
        case FilterModel::SkHP12:   out = un - 2.0f * y[0] + y[1]; break;
        default:                    out = y[N - 1]; break;
    }
    return out * outGain;
}

float Filter::processDigital (float x, int ch) noexcept
{
    if (drive > 0.0f)
        x = tanhA (x * inGain) * outGain;
    auto tick = [] (const Svf& f, float* s, float v0, float& lp, float& bp)
    {
        const float v3 = v0 - s[1];
        const float v1 = f.a1 * s[0] + f.a2 * v3;
        const float v2 = s[1] + f.a2 * s[0] + f.a3 * v3;
        s[0] = 2.0f * v1 - s[0];
        s[1] = 2.0f * v2 - s[1];
        lp = v2; bp = v1;
    };
    float lp, bp;
    switch (mdl)
    {
        case FilterModel::DigHP12: case FilterModel::DigHP24:
            for (int i = 0; i < sections; ++i)
            {
                tick (svf[(size_t) i], ic[ch][i], x, lp, bp);
                x = x - svf[(size_t) i].k * bp - lp;
            }
            return x;
        case FilterModel::DigBP:
            tick (svf[0], ic[ch][0], x, lp, bp);
            return svf[0].k * bp;
        case FilterModel::DigNotch:
            tick (svf[0], ic[ch][0], x, lp, bp);
            return x - svf[0].k * bp;
        case FilterModel::Formant:
        {
            float out = 0.0f;
            for (int i = 0; i < 3; ++i)
            {
                tick (svf[(size_t) i], ic[ch][i], x, lp, bp);
                out += svf[(size_t) i].gain * svf[(size_t) i].k * bp;
            }
            return out * 2.0f;
        }
        default:   // LP cascades
            for (int i = 0; i < sections; ++i)
            {
                tick (svf[(size_t) i], ic[ch][i], x, lp, bp);
                x = lp;
            }
            return x;
    }
}

float Filter::processComb (float x, int ch) noexcept
{
    auto& l = line[ch];
    const int size = (int) l.size();
    float rp = (float) writePos - delaySamples;
    if (rp < 0.0f) rp += (float) size;
    const int i0 = (int) rp;
    const float t = rp - (float) i0;
    const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
    const float d = l[(size_t) i0] + (l[(size_t) i1] - l[(size_t) i0]) * t;
    if (drive > 0.0f) x = tanhA (x * inGain) * outGain;
    float y = x + fb * tanhA (d);
    if (std::fabs (y) < 1.0e-20f) y = 0.0f;
    l[(size_t) writePos] = y;
    return y * (1.0f - 0.5f * std::fabs (fb));
}

void Filter::process (float* left, float* right, int n, float f0, float f1) noexcept
{
    if (n <= 0) return;
    f0 = std::clamp (f0, 20.0f, 20000.0f);
    f1 = std::clamp (f1, 20.0f, 20000.0f);
    const float l0 = std::log2 (f0), dl = std::log2 (f1) - l0;
    const bool analog = isAnalogFilter (mdl);
    const bool comb = mdl == FilterModel::CombPlus || mdl == FilterModel::CombMinus;
    const int size = comb ? (int) line[0].size() : 1;
    if (comb && size < 8) return;   // not prepared

    for (int i0 = 0; i0 < n; i0 += sub)
    {
        const int m = std::min (sub, n - i0);
        const float fc = std::exp2 (l0 + dl * ((float) i0 + 0.5f * (float) m) / (float) n);
        if (fc != lastCutoff || res != lastRes || drive != lastDrive)
        {
            updateCoeffs (fc);
            lastCutoff = fc;
            lastRes = res;
            lastDrive = drive;
        }
        for (int i = i0; i < i0 + m; ++i)
        {
            if (analog)
            {
                left[i] = processAnalog (left[i], 0);
                right[i] = processAnalog (right[i], 1);
            }
            else if (comb)
            {
                left[i] = processComb (left[i], 0);
                right[i] = processComb (right[i], 1);
                if (++writePos >= size) writePos = 0;
            }
            else
            {
                left[i] = processDigital (left[i], 0);
                right[i] = processDigital (right[i], 1);
            }
        }
    }
    sanitize();
}

void Filter::sanitize() noexcept
{
    bool bad = false;
    auto fix = [&bad] (float& v)
    {
        if (! (std::fabs (v) < 1.0e4f)) bad = true;          // NaN / runaway
        else if (std::fabs (v) < 1.0e-15f) v = 0.0f;         // denormal guard
    };
    for (auto& s : st) for (auto& v : s) fix (v);
    for (auto& a : ic) for (auto& s : a) for (auto& v : s) fix (v);
    if (bad)
    {
        for (auto& s : st) for (auto& v : s) v = 0.0f;
        for (auto& a : ic) for (auto& s : a) for (auto& v : s) v = 0.0f;
    }
}
} // namespace chroma
