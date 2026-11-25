#pragma once
#include <cstdint>

namespace chroma
{
// SCALE (§1.1): 12 slots = note classes; each slot is a mask of layers (bit i = Layer i+1).
// Returns the mask of layers that play `note`. Scale off = every eligible layer.
// An empty slot mutes the note.
inline uint16_t scaleLayersForNote (int note, bool scaleOn, const uint16_t (&slotMasks)[12], uint16_t eligibleLayers) noexcept
{
    if (! scaleOn)
        return eligibleLayers;
    const int pc = ((note % 12) + 12) % 12;
    return (uint16_t) (slotMasks[pc] & eligibleLayers & 0x0FFF);
}
} // namespace chroma
