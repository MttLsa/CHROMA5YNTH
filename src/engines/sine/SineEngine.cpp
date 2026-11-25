#include "SineEngine.h"
#include <algorithm>
#include <cmath>

namespace chroma
{
void SineEngine::noteOn (int voice, const EngineNote& n)
{
    if (! n.retrigger)
        return;
    auto& v = voices[(size_t) voice];
    for (size_t u = 0; u < v.phase.size(); ++u)
    {
        v.seed = v.seed * 1664525u + 1013904223u;
        v.phase[u] = u == 0 ? 0.0 : (double) (v.seed >> 8) / 16777216.0;
    }
}

void SineEngine::render (int voice, const EngineVoiceParams& p, float* left, float* right, int n) noexcept
{
    auto& v = voices[(size_t) voice];
    std::fill (left, left + n, 0.0f);
    std::fill (right, right + n, 0.0f);
    const int count = std::clamp (p.unison, 1, maxUnison);
    const float norm = 1.0f / std::sqrt ((float) count);

    for (int u = 0; u < count; ++u)
    {
        const float pos = count > 1 ? (float) u / (float) (count - 1) - 0.5f : 0.0f;  // -0.5..0.5
        const float cents = pos * p.detuneCents + (p.driftSemis != nullptr ? p.driftSemis[u] * 100.0f : 0.0f);
        const float pan = std::clamp (p.unisonPan + 2.0f * pos * p.spread, -1.0f, 1.0f);
        const float angle = (pan + 1.0f) * 0.25f * 3.14159265f;
        const float gl = std::cos (angle) * norm * 1.41421356f, gr = std::sin (angle) * norm * 1.41421356f;

        const double f0 = 440.0 * std::exp2 ((p.pitchStart + cents * 0.01f - 69.0f) / 12.0f) / sr;
        const double f1 = 440.0 * std::exp2 ((p.pitchEnd + cents * 0.01f - 69.0f) / 12.0f) / sr;
        const double df = n > 0 ? (f1 - f0) / n : 0.0;
        double ph = v.phase[(size_t) u], f = f0;
        for (int i = 0; i < n; ++i)
        {
            const float s = (float) std::sin (6.283185307179586 * ph);
            left[i] += s * gl;
            right[i] += s * gr;
            ph += f; f += df;
            if (ph >= 1.0) ph -= 1.0;
        }
        v.phase[(size_t) u] = ph;
    }
}
} // namespace chroma
