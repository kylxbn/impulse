#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

struct BitrateSpan {
    uint32_t frames = 0;
    int64_t  bitrate_bps = 0;
};

// Lock-free SPSC queue that tracks bitrate metadata for decoded audio spans.
// The producer pushes one span per written PCM chunk; the consumer consumes
// spans in playback order as frames are rendered.
template <size_t Capacity>
class BitrateSpanQueue {
public:
    bool push(uint32_t frames, int64_t bitrate_bps) noexcept {
        if (frames == 0) return true;

        const size_t write = write_pos_.load(std::memory_order_relaxed);
        const size_t read  = read_pos_.load(std::memory_order_acquire);
        if (write - read >= Capacity)
            return false;

        spans_[write & kMask] = BitrateSpan{frames, bitrate_bps};
        write_pos_.store(write + 1, std::memory_order_release);
        return true;
    }

    bool canPush() const noexcept {
        const size_t write = write_pos_.load(std::memory_order_acquire);
        const size_t read  = read_pos_.load(std::memory_order_acquire);
        return (write - read) < Capacity;
    }

    int64_t consumeFrames(uint32_t frames) noexcept {
        uint32_t remaining = frames;
        while (remaining > 0) {
            if (front_remaining_frames_ == 0) {
                const size_t read  = read_pos_.load(std::memory_order_relaxed);
                const size_t write = write_pos_.load(std::memory_order_acquire);
                if (read == write) {
                    current_bitrate_bps_ = 0;
                    return 0;
                }

                const BitrateSpan span = spans_[read & kMask];
                read_pos_.store(read + 1, std::memory_order_release);
                front_remaining_frames_ = span.frames;
                front_bitrate_bps_ = span.bitrate_bps;
            }

            const uint32_t consumed = remaining < front_remaining_frames_
                ? remaining
                : front_remaining_frames_;
            remaining -= consumed;
            front_remaining_frames_ -= consumed;
            current_bitrate_bps_ = front_bitrate_bps_;
        }

        return current_bitrate_bps_;
    }

    void reset() noexcept {
        read_pos_.store(0, std::memory_order_relaxed);
        write_pos_.store(0, std::memory_order_relaxed);
        front_remaining_frames_ = 0;
        front_bitrate_bps_ = 0;
        current_bitrate_bps_ = 0;
    }

private:
    static constexpr size_t kMask = Capacity - 1;
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "BitrateSpanQueue capacity must be a power of two");

    alignas(64) std::atomic<size_t> write_pos_{0};
    alignas(64) std::atomic<size_t> read_pos_{0};

    std::array<BitrateSpan, Capacity> spans_{};

    // Consumer-side state only.
    uint32_t front_remaining_frames_ = 0;
    int64_t  front_bitrate_bps_ = 0;
    int64_t  current_bitrate_bps_ = 0;
};
