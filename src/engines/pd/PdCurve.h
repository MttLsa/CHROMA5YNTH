#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace chroma::pd
{
// Custom phase curve (§3E, point editor): user points with interpolation, published to the audio
// thread as a lock-free table. The end points are pinned to (0,0) and (1,1) so the distorted phase
// always closes the cycle (phase continuity).
class PhaseCurve
{
public:
    enum Interp { Linear, Smooth, Cubic };
    static constexpr int tableSize = 256, maxPoints = 32;
    struct Point { float x = 0.0f, y = 0.0f; };

    PhaseCurve() { reset(); }

    void reset() { setPoints ({ { 0.0f, 0.0f }, { 1.0f, 1.0f } }, Linear); }

    // Message thread only.
    void setPoints (std::vector<Point> p, int interpolation)
    {
        for (auto& q : p)
        {
            q.x = std::isfinite (q.x) ? std::clamp (q.x, 0.0f, 1.0f) : 0.0f;
            q.y = std::isfinite (q.y) ? std::clamp (q.y, 0.0f, 1.0f) : 0.0f;
        }
        std::stable_sort (p.begin(), p.end(), [] (const Point& a, const Point& b) { return a.x < b.x; });
        p.erase (std::remove_if (p.begin(), p.end(), [] (const Point& q) { return q.x <= 0.0f || q.x >= 1.0f; }), p.end());
        if (p.size() > (size_t) maxPoints - 2)
            p.resize ((size_t) maxPoints - 2);
        p.insert (p.begin(), Point { 0.0f, 0.0f });
        p.push_back ({ 1.0f, 1.0f });
        pts = std::move (p);
        interp = std::clamp (interpolation, 0, 2);
        for (int i = 0; i <= tableSize; ++i)
            table[(size_t) i].store (interpolate (pts, interp, (float) i / (float) tableSize), std::memory_order_relaxed);
    }

    const std::vector<Point>& points() const noexcept { return pts; }
    int interpolation() const noexcept { return interp; }

    // Audio thread: x in [0, 1].
    float evaluate (float x) const noexcept
    {
        const float f = std::clamp (x, 0.0f, 1.0f) * (float) tableSize;
        const int i = std::min ((int) f, tableSize - 1);
        const float a = table[(size_t) i].load (std::memory_order_relaxed);
        const float b = table[(size_t) i + 1].load (std::memory_order_relaxed);
        return a + (b - a) * (f - (float) i);
    }

    // Exact curve through sorted points (x strictly inside [0,1] pinned by the end points).
    static float interpolate (const std::vector<Point>& p, int mode, float x) noexcept
    {
        if (p.size() < 2)
            return x;
        size_t k = 0;
        while (k + 2 < p.size() && x > p[k + 1].x)
            ++k;
        const Point& a = p[k];
        const Point& b = p[k + 1];
        const float w = b.x - a.x;
        const float u = w > 1.0e-6f ? std::clamp ((x - a.x) / w, 0.0f, 1.0f) : 1.0f;
        if (mode == Smooth)
            return a.y + (b.y - a.y) * (0.5f - 0.5f * std::cos (3.14159265f * u));
        if (mode == Cubic)
        {
            // Catmull-Rom on non-uniform x using finite-difference tangents (scaled to the segment).
            const auto tangent = [&] (size_t i)
            {
                const size_t l = i > 0 ? i - 1 : i, r = i + 1 < p.size() ? i + 1 : i;
                const float dx = p[r].x - p[l].x;
                return dx > 1.0e-6f ? (p[r].y - p[l].y) / dx : 0.0f;
            };
            const float m0 = tangent (k) * w, m1 = tangent (k + 1) * w;
            const float u2 = u * u, u3 = u2 * u;
            return (2 * u3 - 3 * u2 + 1) * a.y + (u3 - 2 * u2 + u) * m0 + (-2 * u3 + 3 * u2) * b.y + (u3 - u2) * m1;
        }
        return a.y + (b.y - a.y) * u;
    }

private:
    std::array<std::atomic<float>, tableSize + 1> table;
    std::vector<Point> pts;
    int interp = Linear;
};
} // namespace chroma::pd
