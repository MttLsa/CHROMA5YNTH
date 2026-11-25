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
    // Writes (does not add) n stereo samples.
    virtual void render (int voice, const EngineVoiceParams&, float* left, float* right, int n) noexcept = 0;
};
} // namespace chroma
