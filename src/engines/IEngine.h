#pragma once

namespace chroma
{
inline constexpr int maxPoolVoices = 36;
inline constexpr int maxUnison = 16;

struct EngineNote
{
    int note = 60;
    float velocity = 1.0f;
    int channel = 0;
    bool retrigger = true;     // false = legato transition (keep oscillator state)
};

// Per-note MPE expression, updated at control rate.
struct NoteExpression
{
    float pitchBendSemis = 0.0f, pressure = 0.0f, slide = 0.5f;
};

// Values the voice computes at control rate and hands to the engine for one render call.
struct EngineVoiceParams
{
    float pitchStart = 60.0f, pitchEnd = 60.0f;   // MIDI note (fractional), interpolated across the call
    int unison = 1;
    float detuneCents = 0.0f, spread = 0.0f, unisonPan = 0.0f;
    const float* driftSemis = nullptr;            // DRIFT: per-oscillator pitch offset [maxUnison], null = off
    const float* base = nullptr;                  // layer plain values [lp::count] (smoothed)
    const float* offsets = nullptr;               // per-voice normalised modulation offsets [lp::count], may be null
    float* bypassL = nullptr;                     // optional post-filter bus (Mixer routing "Bypass")
    float* bypassR = nullptr;
};

// Common engine interface (§1). Engines keep per-voice state indexed by the global voice index.
class IEngine
{
public:
    virtual ~IEngine() = default;
    virtual void prepare (double sampleRate, int maxBlockSize, int maxVoices) = 0;
    virtual void reset() = 0;
    virtual void noteOn (int voice, const EngineNote&) = 0;
    virtual void noteOff (int voice) = 0;
    virtual void kill (int voice) = 0;
    virtual void setNoteExpression (int /*voice*/, const NoteExpression&) {}
    virtual void setOversampling (int /*factor 1/2/4/8*/) {}
    // Writes (does not add) n stereo samples to the filter bus. Returns true when it also wrote
    // n samples to p.bypassL/R (signal routed around the voice filter); false = bypass bus untouched.
    virtual bool render (int voice, const EngineVoiceParams& p, float* left, float* right, int n) noexcept = 0;
};
} // namespace chroma
