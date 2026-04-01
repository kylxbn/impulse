#pragma once

#include <atomic>
#include <cstdint>

enum class PlaybackStatus : int {
    Stopped,
    Buffering,
    Playing,
    Paused,
    Seeking,
    EndOfTrack,
    Error
};

// All fields are atomics: safe to read from any thread without a mutex.
struct PlaybackState {
    std::atomic<PlaybackStatus> status{PlaybackStatus::Stopped};
    std::atomic<int64_t>        current_frame{0};       // absolute frames consumed by PipeWire
    std::atomic<float>          volume{1.0f};           // observed PipeWire stream volume
    std::atomic<int64_t>        current_bitrate_bps{0};
    std::atomic<float>          current_peak_abs{0.0f};
    std::atomic<float>          current_rms_abs{0.0f};
    std::atomic<bool>           clipped_detected{false};
    std::atomic<int>            seek_generation{0};     // incremented on each seek
    std::atomic<bool>           seek_in_progress{false};
    std::atomic<bool>           decoder_reached_eof{false};
};
