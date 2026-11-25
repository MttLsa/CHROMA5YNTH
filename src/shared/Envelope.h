#pragma once
#include <algorithm>
#include <cmath>

namespace chroma
{
// ENV (§2): Attack/Hold/Decay/Sustain/Release, curve per segment, velocity, key tracking,
// loop, retrigger, time scaling, amount.
struct EnvParams
{
    float attack = 0.005f, hold = 0.0f, decay = 0.3f, sustain = 0.8f, release = 0.3f;
    int attackCurve = 0, decayCurve = 1, releaseCurve = 1; // 0 lin, 1 exp, 2 log
    float velocity = 0.0f, keyTrack = 0.0f;
    bool loop = false, retrigger = true;
    float timeScale = 1.0f, amount = 1.0f;
};

class Envelope
{
public:
    enum class Stage { Idle, Attack, Hold, Decay, Sustain, Release };

    void setSampleRate (double sr) noexcept { sampleRate = (float) sr; }

    void noteOn (const EnvParams& p, float vel, int note) noexcept
    {
        velScale = 1.0f - p.velocity + p.velocity * vel;
        keyFactor = std::exp2 (-p.keyTrack * (float) (note - 60) / 12.0f);
        if (stage == Stage::Idle || p.retrigger)
            level = 0.0f;
        start (Stage::Attack, p);
    }

    void noteOff (const EnvParams& p) noexcept
    {
        if (stage != Stage::Idle && stage != Stage::Release)
            start (Stage::Release, p);
    }

    void kill() noexcept { stage = Stage::Idle; level = 0.0f; }

    // Advance n samples; returns the raw level (0..1).
    float advance (const EnvParams& p, int n) noexcept
    {
        switch (stage)
        {
            case Stage::Idle: return level = 0.0f;
            case Stage::Sustain:
                level = p.sustain;
                return level;
            default: break;
        }
        t += inc * (float) n;
        const float target = stage == Stage::Attack ? 1.0f : stage == Stage::Hold ? 1.0f
                           : stage == Stage::Decay ? p.sustain : 0.0f;
        const int curve = stage == Stage::Attack ? p.attackCurve : stage == Stage::Decay ? p.decayCurve : p.releaseCurve;
        if (t >= 1.0f)
        {
            level = target;
            switch (stage)
            {
                case Stage::Attack:  start (Stage::Hold, p); break;
                case Stage::Hold:    start (Stage::Decay, p); break;
                case Stage::Decay:   if (p.loop) { from = level; start (Stage::Attack, p); } else stage = Stage::Sustain; break;
                case Stage::Release: stage = Stage::Idle; level = 0.0f; break;
                default: break;
            }
        }
        else
        {
            level = stage == Stage::Hold ? 1.0f : from + (target - from) * shape (t, curve);
        }
        return level;
    }

    float next (const EnvParams& p) noexcept { return advance (p, 1); }

    float output (const EnvParams& p) const noexcept { return level * velScale * p.amount; }
    float levelRaw() const noexcept { return level; }
    float velocityScale() const noexcept { return velScale; }
    bool isActive() const noexcept { return stage != Stage::Idle; }
    bool isReleasing() const noexcept { return stage == Stage::Release; }
    Stage getStage() const noexcept { return stage; }

private:
    static float shape (float x, int curve) noexcept
    {
        constexpr float k = 5.0f, norm = 1.0f / (1.0f - 0.006737947f); // 1/(1-e^-5)
        switch (curve)
        {
            case 1:  return (1.0f - std::exp (-k * x)) * norm;               // fast then slow
            case 2:  return (std::exp (k * (x - 1.0f)) - 0.006737947f) * norm; // slow then fast
            default: return x;
        }
    }

    void start (Stage s, const EnvParams& p) noexcept
    {
        stage = s;
        from = level;
        t = 0.0f;
        float time = s == Stage::Attack ? p.attack : s == Stage::Hold ? p.hold : s == Stage::Decay ? p.decay : p.release;
        time *= p.timeScale * keyFactor;
        const float minTime = (s == Stage::Hold) ? 0.0f : 0.0002f;
        if (time <= minTime && s == Stage::Hold) { inc = 2.0f; return; } // skip next advance
        inc = 1.0f / (std::max (time, minTime) * sampleRate);
    }

    float sampleRate = 48000.0f;
    Stage stage = Stage::Idle;
    float level = 0.0f, from = 0.0f, t = 0.0f, inc = 0.0f;
    float velScale = 1.0f, keyFactor = 1.0f;
};
} // namespace chroma
