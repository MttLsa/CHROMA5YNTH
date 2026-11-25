#pragma once
#include <array>

namespace chroma
{
// Controller state per MIDI channel (0-based), plus MPE zone configuration.
struct ChannelState
{
    float bend = 0.0f;            // -1..1
    float pressure = 0.0f;        // 0..1
    std::array<float, 128> cc {};
    std::array<float, 128> polyPressure {};
    bool sustain = false, sostenuto = false;

    void reset() noexcept
    {
        bend = pressure = 0.0f;
        cc.fill (0.0f);
        polyPressure.fill (0.0f);
        cc[7] = 100.0f / 127.0f;
        cc[11] = 1.0f;   // expression
        cc[74] = 0.5f;   // MPE slide neutral
        sustain = sostenuto = false;
    }
};

struct MpeConfig
{
    bool enabled = false;
    bool upperZone = false;
    int bendRange = 48;

    int masterChannel() const noexcept { return upperZone ? 15 : 0; }
    bool isMember (int ch) const noexcept { return enabled && ch != masterChannel(); }
    // Channel holding global controllers for a voice on `ch`.
    int globalChannel (int ch) const noexcept { return enabled ? masterChannel() : ch; }
};

struct MidiState
{
    std::array<ChannelState, 16> channels;
    MidiState() { reset(); }
    void reset() noexcept { for (auto& c : channels) c.reset(); }
};
} // namespace chroma
