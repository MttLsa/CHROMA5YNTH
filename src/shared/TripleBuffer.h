#pragma once
#include <array>
#include <atomic>

namespace chroma
{
// Lock-free snapshot exchange: one writer (message thread), one reader (audio thread).
// The writer edits `edit()` and calls publish(); the reader calls read() once per block.
template <typename T>
class TripleBuffer
{
public:
    TripleBuffer() { buffers.fill (staging); }

    T& edit() noexcept { return staging; }
    const T& latest() const noexcept { return staging; }

    void publish() noexcept
    {
        buffers[(size_t) backIndex] = staging;
        backIndex = middle.exchange (backIndex | dirtyBit, std::memory_order_acq_rel) & indexMask;
    }

    const T& read() noexcept
    {
        if (middle.load (std::memory_order_acquire) & dirtyBit)
            frontIndex = middle.exchange (frontIndex, std::memory_order_acq_rel) & indexMask;
        return buffers[(size_t) frontIndex];
    }

private:
    static constexpr int dirtyBit = 4, indexMask = 3;
    T staging {};
    std::array<T, 3> buffers {};
    std::atomic<int> middle { 1 };
    int backIndex = 0, frontIndex = 2;
};
} // namespace chroma
