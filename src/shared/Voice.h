#pragma once
#include "Drift.h"
#include "Envelope.h"
#include "Filter.h"
#include "Smoothing.h"
#include "VoiceContext.h"
#include <cstdint>

namespace chroma
{
struct VoiceStart
{
    int layer = 0, channel = 0, note = 60;
    float velocity = 1.0f;        // raw 0..1
    uint64_t time = 0;            // note-on timestamp (samples) for stealing
    float glideFrom = -1.0f;      // < 0 = no glide
};

// VOICE (§2): one pool voice. Unison lives inside the voice (engine side) and does not use the pool.
class Voice
{
public:
    enum class State { Free, Playing, Stealing };

    void prepare (int voiceIndex, double sampleRate);
    void start (const VoiceStart&, VoiceGlobals&);
    void steal (const VoiceStart&, VoiceGlobals&);
    void legato (int note, float velocity, bool retrigger, bool glide, VoiceGlobals&);
    void release (float releaseVelocity, VoiceGlobals&);   // envelopes to release stage
    void kill (VoiceGlobals&);

    void controlTick (VoiceGlobals&, int samples) noexcept;
    void render (VoiceGlobals&, int start, int numSamples) noexcept;

    State state = State::Free;
    int layer = 0, channel = 0, note = 60;
    bool gate = false;                 // key physically held
    bool sustained = false;            // released key held by sustain/sostenuto pedal
    bool sostenutoLatched = false;
    uint64_t startTime = 0;
    float currentLevel = 0.0f;
    bool pendingRelease = false;
    float glidePitch = 60.0f;          // note portion of the pitch (with glide)

    // Mod matrix offsets (normalised, by lp::Id) of the last control tick.
    const float* modOffsets() const noexcept { return offsets.data(); }

private:
    float val (const LayerRuntime&, int id) const noexcept;
    EnvParams makeEnv (const LayerRuntime&, int i) const noexcept;
    LfoParams makeLfo (const LayerRuntime&, int i) const noexcept;
    void begin (const VoiceStart&, VoiceGlobals&);
    void freeVoice (VoiceGlobals&);

    int index = 0;
    double sr = 48000.0;
    float velocity = 1.0f, releaseVelocity = 0.0f, randomValue = 0.0f;
    uint32_t seed = 1;

    std::array<Envelope, 4> envs {};
    std::array<EnvParams, 4> envP {};
    std::array<Lfo, 4> lfos {};
    std::array<LfoParams, 4> lfoP {};
    ModMatrixState matrix;
    std::array<float, lp::count> offsets {};
    std::array<float, 4> lfoOut {};

    float glideTarget = 60.0f, glideStep = 0.0f, glideTime = 0.0f;
    bool gliding = false;

    EngineVoiceParams ep;
    float pitchStart = 60.0f, pitchEnd = 60.0f;
    bool havePitch = false;
    int tickLen = controlBlock, tickPos = 0;
    LinearRamp gainL, gainR;

    // FILTER LIBRARY + DRIFT (§2)
    Filter filter;
    Drift drift;
    bool filterActive = false, driftActive = false, haveCutoff = false;
    float cutStart = 1000.0f, cutEnd = 1000.0f;   // Hz, interpolated across the tick
    void updateFilter (const LayerRuntime&, float dt) noexcept;
    void renderEngine (IEngine&, EngineVoiceParams, VoiceGlobals&, int n, float p0, float p1) noexcept;
    void runFilter (float* l, float* r, int n, float p0, float p1) noexcept;

    // stealing fade
    VoiceStart pending;
    int fadeLayer = 0, fadeRemaining = 0, fadeTotal = 1;
    float fadeGainL = 0.0f, fadeGainR = 0.0f;
};
} // namespace chroma
