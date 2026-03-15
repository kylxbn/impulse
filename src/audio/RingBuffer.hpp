#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstring>

// Lock-free, single-producer / single-consumer ring buffer.
//
// Capacity must be a power of two.  Interleaved float samples are the
// intended element type, but the template is generic.
//
// Thread-safety contract:
//   - Exactly ONE thread calls write() at any time  (the decode thread).
//   - Exactly ONE thread calls read()  at any time  (the PipeWire callback).
//   - All other methods (available_read, available_write, reset) must only
//     be called when both threads agree the buffer is quiescent (seek).
template <typename T, size_t Capacity>
class RingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "RingBuffer capacity must be a power of two");

public:
    // Returns the number of elements actually written (may be < count if full).
    size_t write(const T* src, size_t count) noexcept {
        const size_t available = Capacity - (write_pos_.load(std::memory_order_relaxed)
                                           - read_pos_.load(std::memory_order_acquire));
        if (count > available)
            count = available;

        const size_t wp    = write_pos_.load(std::memory_order_relaxed) & kMask;
        const size_t chunk = std::min(count, Capacity - wp);

        std::memcpy(buf_.data() + wp,         src,         chunk * sizeof(T));
        std::memcpy(buf_.data(),               src + chunk, (count - chunk) * sizeof(T));

        write_pos_.fetch_add(count, std::memory_order_release);
        return count;
    }

    // Returns the number of elements actually read (may be < count if empty).
    size_t read(T* dst, size_t count) noexcept {
        const size_t available = write_pos_.load(std::memory_order_acquire)
                               - read_pos_.load(std::memory_order_relaxed);
        if (count > available)
            count = available;

        const size_t rp    = read_pos_.load(std::memory_order_relaxed) & kMask;
        const size_t chunk = std::min(count, Capacity - rp);

        std::memcpy(dst,         buf_.data() + rp, chunk * sizeof(T));
        std::memcpy(dst + chunk, buf_.data(),       (count - chunk) * sizeof(T));

        read_pos_.fetch_add(count, std::memory_order_release);
        return count;
    }

    size_t available_read()  const noexcept {
        return write_pos_.load(std::memory_order_acquire)
             - read_pos_.load(std::memory_order_relaxed);
    }

    size_t available_write() const noexcept {
        return Capacity - available_read();
    }

    // Snapshot helpers for UI/telemetry. These are safe to sample concurrently
    // because they only read the producer/consumer atomics.
    size_t write_position() const noexcept {
        return write_pos_.load(std::memory_order_acquire);
    }

    size_t read_position() const noexcept {
        return read_pos_.load(std::memory_order_acquire);
    }

    // UNSAFE: only call when both producer and consumer have agreed to quiesce
    // (e.g., during the seek protocol).
    void reset() noexcept {
        read_pos_.store(0, std::memory_order_relaxed);
        write_pos_.store(0, std::memory_order_relaxed);
    }

private:
    static constexpr size_t kMask = Capacity - 1;

    // Separate cache lines to avoid false sharing between producer and consumer.
    alignas(64) std::atomic<size_t> write_pos_{0};
    alignas(64) std::atomic<size_t> read_pos_{0};

    std::array<T, Capacity> buf_{};
};
