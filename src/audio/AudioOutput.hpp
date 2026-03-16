#pragma once

#include "BitrateSpanQueue.hpp"
#include "RingBuffer.hpp"
#include "core/AudioFormat.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

struct pw_thread_loop;
struct pw_stream;
struct pw_stream_events;

// Wraps a PipeWire stream for real-time stereo f32 playback.
//
// The on_process callback runs on PipeWire's managed realtime thread.
// Rules enforced in that callback:
//   - No malloc / free
//   - No mutex
//   - No blocking syscalls
//   - No printf / logging
//
// All shared state between threads is communicated via atomics only.
class AudioOutput {
public:
    using WakeCallback = void (*)(void*);

    static constexpr size_t kRingCapacity = 1 << 21;  // 2M floats (~21.8s at 48kHz stereo)
    // Streams can arrive in very small decoded chunks while playback is still
    // paused for startup buffering, so keep generous headroom here.
    static constexpr size_t kBitrateSpanCapacity = 1 << 16;

    using Ring = RingBuffer<float, kRingCapacity>;
    using BitrateRing = BitrateSpanQueue<kBitrateSpanCapacity>;

    struct Config {
        uint32_t sample_rate   = 48000;
        uint32_t channels      = 2;
        uint32_t buffer_frames = 1024;
    };

    AudioOutput();
    ~AudioOutput();

    // Non-copyable
    AudioOutput(const AudioOutput&)            = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    // Initialize PipeWire stream.  ring, frame_counter, and seek_generation
    // must outlive this object.
    bool init(Ring&                  ring,
              BitrateRing&           bitrate_ring,
              std::atomic<int64_t>&  frame_counter,
              std::atomic<int>&      seek_generation,
              std::atomic<int64_t>&  current_bitrate_bps,
              std::atomic<float>&    stream_volume,
              WakeCallback           wake_callback = nullptr,
              void*                  wake_userdata = nullptr);

    void shutdown();

    // Thread-safe stream volume control (linear gain converted from the UI's dB slider).
    void setVolume(float linear_gain);
    float volume() const;
    void waitForSeekGeneration(int generation,
                               std::chrono::milliseconds poll_interval = std::chrono::milliseconds{1});
    void setPaused(bool paused) {
        paused_.store(paused, std::memory_order_relaxed);
        if (paused)
            underrun_detected_.store(false, std::memory_order_relaxed);
    }
    bool paused() const { return paused_.load(std::memory_order_relaxed); }
    bool consumeUnderrunDetected() {
        return underrun_detected_.exchange(false, std::memory_order_acq_rel);
    }

    const Config& config() const { return config_; }

private:
    static void onProcess(void* userdata);
    static void onControlInfo(void* userdata, uint32_t id, const struct pw_stream_control* control);

    void applyPendingVolumeLocked();
    bool applyVolumeLocked(float linear_gain);
    void updateObservedVolume(uint32_t id, const struct pw_stream_control* control);

    pw_thread_loop*   loop_   = nullptr;
    pw_stream*        stream_ = nullptr;
    pw_stream_events* events_ = nullptr;

    // Shared state (not owned)
    Ring*                  ring_        = nullptr;
    BitrateRing*           bitrate_ring_ = nullptr;
    std::atomic<int64_t>*  frame_ctr_   = nullptr;
    std::atomic<int>*      seek_gen_    = nullptr;
    std::atomic<int64_t>*  current_bitrate_bps_ = nullptr;
    std::atomic<float>*    observed_volume_ = nullptr;

    // PW-thread-local (only written from on_process)
    int last_seek_gen_ = 0;
    std::atomic<int> observed_seek_gen_{0};

    std::atomic<bool>  paused_{false};
    std::atomic<bool>  stream_active_{false};
    std::atomic<bool>  underrun_detected_{false};
    Config             config_;
    WakeCallback       wake_callback_ = nullptr;
    void*              wake_userdata_ = nullptr;

    // Protected by the PipeWire thread-loop lock.
    bool     has_pending_volume_ = false;
    float    pending_volume_ = 1.0f;
    bool     has_channel_volume_control_ = false;
    bool     has_scalar_volume_control_ = false;
    uint32_t channel_volume_count_ = 0;
};
