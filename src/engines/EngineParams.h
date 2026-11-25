#pragma once
#include "IEngine.h"
#include "shared/ParamRange.h"

namespace chroma
{
// Modulated plain value of a layer parameter for one voice (same law as Voice::val).
inline float engineParam (const EngineVoiceParams& p, int id) noexcept
{
    const float b = p.base != nullptr ? p.base[id] : lp::descs[(size_t) id].def;
    const float o = p.offsets != nullptr ? p.offsets[id] : 0.0f;
    if (o == 0.0f)
        return b;
    const auto& r = paramRange (id);
    return r.fromNorm (r.toNorm (b) + o);
}

inline bool engineSwitch (const EngineVoiceParams& p, int id) noexcept { return engineParam (p, id) > 0.5f; }
inline int engineChoice (const EngineVoiceParams& p, int id) noexcept { return (int) std::lround (engineParam (p, id)); }
} // namespace chroma
