#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

struct PeakSpan {
    uint32_t frames = 0;
    float    peak_abs = 0.0f;
    bool     clipped = false;
};

struct PeakConsumeResult {
    float peak_abs = 0.0f;
    bool  clipped = false;
};

// Lock-free SPSC queue that tracks peak/clipping telemetry for decoded audio
// spans. The producer pushes one span per written PCM chunk; the consumer
// consumes spans in playback order as frames are rendered.
template <size_t Capacity>
class PeakSpanQueue {
public:
    bool push(uint32_t frames, float peak_abs, bool clipped) noexcept {
        if (frames == 0) return true;

        const size_t write = write_pos_.load(std::memory_order_relaxed);
        const size_t read  = read_pos_.load(std::memory_order_acquire);
        if (write - read >= Capacity)
            return false;

        spans_[write & kMask] = PeakSpan{frames, peak_abs, clipped};
        write_pos_.store(write + 1, std::memory_order_release);
        return true;
    }

    bool canPush() const noexcept {
        const size_t write = write_pos_.load(std::memory_order_acquire);
        const size_t read  = read_pos_.load(std::memory_order_acquire);
        return (write - read) < Capacity;
    }

    PeakConsumeResult consumeFrames(uint32_t frames) noexcept {
        PeakConsumeResult result{};
        uint32_t remaining = frames;
        while (remaining > 0) {
            if (front_remaining_frames_ == 0) {
                const size_t read  = read_pos_.load(std::memory_order_relaxed);
                const size_t write = write_pos_.load(std::memory_order_acquire);
                if (read == write)
                    return result;

                const PeakSpan span = spans_[read & kMask];
                read_pos_.store(read + 1, std::memory_order_release);
                front_remaining_frames_ = span.frames;
                front_peak_abs_ = span.peak_abs;
                front_clipped_ = span.clipped;
            }

            const uint32_t consumed = remaining < front_remaining_frames_
                ? remaining
                : front_remaining_frames_;
            remaining -= consumed;
            front_remaining_frames_ -= consumed;
            if (front_peak_abs_ > result.peak_abs)
                result.peak_abs = front_peak_abs_;
            result.clipped = result.clipped || front_clipped_;
        }

        return result;
    }

    void reset() noexcept {
        read_pos_.store(0, std::memory_order_relaxed);
        write_pos_.store(0, std::memory_order_relaxed);
        front_remaining_frames_ = 0;
        front_peak_abs_ = 0.0f;
        front_clipped_ = false;
    }

private:
    static constexpr size_t kMask = Capacity - 1;
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "PeakSpanQueue capacity must be a power of two");

    alignas(64) std::atomic<size_t> write_pos_{0};
    alignas(64) std::atomic<size_t> read_pos_{0};

    std::array<PeakSpan, Capacity> spans_{};

    // Consumer-side state only.
    uint32_t front_remaining_frames_ = 0;
    float    front_peak_abs_ = 0.0f;
    bool     front_clipped_ = false;
};
