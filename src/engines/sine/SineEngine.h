#pragma once
#include "engines/IEngine.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace chroma
{
// Test engine for the core (phase 1): unison sine stack.
class SineEngine final : public IEngine
{
public:
    void prepare (double sampleRate, int, int) override { sr = sampleRate; reset(); }
    void reset() override { for (auto& v : voices) v = {}; }
    void noteOn (int voice, const EngineNote& n) override;
    void noteOff (int) override {}
    void kill (int voice) override { voices[(std::size_t) voice] = {}; }
    bool render (int voice, const EngineVoiceParams&, float* left, float* right, int n) noexcept override;

private:
    struct VoiceState { std::array<double, maxUnison> phase {}; uint32_t seed = 0x9e3779b9u; };
    std::array<VoiceState, maxPoolVoices> voices {};
    double sr = 48000.0;
};
} // namespace chroma
