#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <complex>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chroma::wt
{
inline constexpr int frameSize = 2048;          // samples per frame (import/export format)
inline constexpr int maxFrames = 256;
inline constexpr int numLevels = 10;            // octave mipmaps
inline constexpr int maxHarmonics = 512;        // harmonics kept at level 0
inline constexpr int numUserSlots = 3;

constexpr int levelHarmonics (int l) noexcept { return maxHarmonics >> l; }
constexpr int levelLength (int l) noexcept { return std::max (frameSize >> l, 256); }  // >= 4 samples per top harmonic
constexpr int levelOffset (int l) noexcept
{
    int o = 0;
    for (int i = 0; i < l; ++i) o += levelLength (i) + 4;   // 1 guard sample before, 3 after (cubic wrap)
    return o;
}
inline constexpr int frameStride = levelOffset (numLevels);
inline constexpr int specSize = maxHarmonics + 1;            // complex coefficients, index = harmonic number

// Lowest mip level whose top harmonic stays below Nyquist for `cyclesPerSample` (fundamental / base sample rate).
inline int levelFor (float cyclesPerSample) noexcept
{
    const float hMax = 0.5f / std::max (cyclesPerSample, 1.0e-9f);
    int l = 0;
    while (l < numLevels - 1 && (float) levelHarmonics (l) > hMax) ++l;
    return l;
}

// Band-limited wavetable: per frame, octave mipmaps (cubic-interpolation ready: p[-1] and p[len..len+2] are
// wrap guards) + the complex harmonic spectrum (amplitude convention: x(t) = sum Re(c_k e^{i 2 pi k t})).
struct Table
{
    std::string name;
    int numFrames = 0;
    std::vector<float> mip;                      // numFrames * frameStride
    std::vector<std::complex<float>> spec;       // numFrames * specSize

    const float* level (int frame, int l) const noexcept
    {
        return mip.data() + (size_t) frame * frameStride + (size_t) levelOffset (l) + 1;
    }
    const std::complex<float>* spectrum (int frame) const noexcept { return spec.data() + (size_t) frame * specSize; }
};

using TablePtr = std::shared_ptr<const Table>;

// Builds the mipmaps from raw frames (frameSize samples each). DC is removed. normalize = peak of the
// band-limited table scaled to 0.99. Not real-time safe (allocates, FFT).
TablePtr buildTable (const float* frames, int numFrames, const std::string& name, bool normalize = true);

// Inverse FFT helper: writes len samples + wrap guards out[-1], out[len..len+2] of sum_{k=1..H} Re(c_k e^{i2pi k n/len}). len = power of 2.
// work: >= 2 * len floats. Real-time safe (uses a preplanned FFT).
void synthesize (const std::complex<float>* c, int harmonics, float* out, int len, float* work) noexcept;

// Forward analysis: frame of frameSize samples -> c[0..specSize). Not real-time safe.
void analyse (const float* frame, std::complex<float>* c);
// Full analysis: c[0] = DC, c[1..frameSize/2) harmonics (same convention). Not real-time safe.
void analyseFull (const float* frame, std::complex<float>* c);

// Factory bank (§3D): procedurally generated tables, built once per process (thread-safe, off the audio thread).
struct FactoryBank
{
    std::vector<TablePtr> tables;
    TablePtr sub;                                // Sine, Triangle, Saw, Square (Sub OSC)
};
const FactoryBank& factory();
int numFactoryTables() noexcept;
const char* factoryName (int index) noexcept;
// Raw (not band-limited) frames of a factory table, for the editor.
std::vector<float> factoryFrames (int index, int& numFrames);

// Lock-free publication of a user table to the audio thread (§3D editor). Message thread: set(); audio: get().
// Replaced tables are kept until the audio thread has completed two blocks after the swap.
class TableSlot
{
public:
    void set (TablePtr t);
    const Table* get() const noexcept { return current.load (std::memory_order_acquire); }
    TablePtr owner() const { return own; }
    static void audioBlockDone() noexcept { epoch().fetch_add (1, std::memory_order_acq_rel); }

private:
    static std::atomic<uint64_t>& epoch() noexcept { static std::atomic<uint64_t> e { 0 }; return e; }
    std::atomic<const Table*> current { nullptr };
    TablePtr own;
    std::vector<std::pair<uint64_t, TablePtr>> retired;
};
} // namespace chroma::wt
