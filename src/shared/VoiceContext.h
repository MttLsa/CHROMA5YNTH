#pragma once
#include "LayerParams.h"
#include "Lfo.h"
#include "MidiState.h"
#include "ModMatrix.h"
#include "engines/IEngine.h"
#include <array>

namespace chroma
{
inline constexpr int numLayers = 12;
inline constexpr int controlBlock = 32;   // control-rate period in samples

// Per-layer data computed by the synth at control rate and read by the voices.
struct LayerRuntime
{
    std::array<float, lp::count> base {};          // smoothed plain values (store + automation + macros)
    std::array<ModSlot, modSlots> slots {};
    IEngine* engine = nullptr;
    std::array<const LfoCustomTable*, 4> custom {};
    std::array<double, 4> freePhase {};
    float transpose = 0.0f;
};

struct VoiceGlobals
{
    const MidiState* midi = nullptr;
    MpeConfig mpe;
    std::array<float, 8> macros {};
    double bpm = 120.0, sampleRate = 48000.0;
    std::array<LayerRuntime, numLayers>* layers = nullptr;
    std::array<float*, numLayers> layerL {}, layerR {};   // chunk-relative layer buffers
    float* scratchL = nullptr;
    float* scratchR = nullptr;
    float* bypassL = nullptr;                             // engine bypass bus (added after the voice filter)
    float* bypassR = nullptr;
    int tickRemaining = controlBlock;                     // samples left in the current control tick at chunk start
};
} // namespace chroma
