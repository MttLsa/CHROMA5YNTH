#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace chroma
{
// Import of 6-operator 32-voice bank files (.syx, packed 128-byte voice format) [§3C, file format only].
// Each voice becomes a set of FM6 layer parameter values (lp::Id, plain value); envelope rates/levels
// are approximated by the ADSR envelopes.
struct FmPatch
{
    std::string name;
    std::vector<std::pair<int, float>> values;
};

// Accepts a full bulk dump (4104 bytes: header, 4096 data bytes, checksum, F7) or the raw 4096 bytes.
// Returns false (and leaves `out` empty) when the data is not a valid 32-voice bank.
bool parseFmBank (const uint8_t* data, size_t size, std::vector<FmPatch>& out);

// Converts one packed 128-byte voice.
FmPatch convertFmVoice (const uint8_t* packed);

// Approximate time in seconds of a full envelope segment at rate 0..99.
float fmRateToSeconds (int rate) noexcept;
} // namespace chroma
