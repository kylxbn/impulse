#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

struct PeakMeterReading {
    float peak_abs = 0.0f;
    bool  clipped = false;
};

class PeakMeterAccumulator {
public:
    explicit PeakMeterAccumulator(uint32_t window_frames = 1) noexcept
        : window_frames_(std::max(window_frames, 1u)) {}

    void setWindowFrames(uint32_t window_frames) noexcept {
        window_frames_ = std::max(window_frames, 1u);
        reset();
    }

    [[nodiscard]] uint32_t windowFrames() const noexcept { return window_frames_; }

    std::optional<PeakMeterReading> consume(const float* samples,
                                            uint32_t     frames,
                                            uint32_t     channels) noexcept {
        if (!samples || frames == 0 || channels == 0)
            return std::nullopt;

        std::optional<PeakMeterReading> latest_completed_window;
        uint32_t remaining_frames = frames;
        size_t sample_offset = 0;

        while (remaining_frames > 0) {
            const uint32_t chunk_frames =
                std::min(remaining_frames, window_frames_ - accumulated_frames_);
            updateWindow(samples + sample_offset,
                         static_cast<size_t>(chunk_frames) * channels);

            accumulated_frames_ += chunk_frames;
            sample_offset += static_cast<size_t>(chunk_frames) * channels;
            remaining_frames -= chunk_frames;

            if (accumulated_frames_ == window_frames_) {
                latest_completed_window = PeakMeterReading{
                    .peak_abs = peak_abs_,
                    .clipped = clipped_,
                };
                reset();
            }
        }

        return latest_completed_window;
    }

    std::optional<PeakMeterReading> flush() noexcept {
        if (accumulated_frames_ == 0)
            return std::nullopt;

        const PeakMeterReading reading{
            .peak_abs = peak_abs_,
            .clipped = clipped_,
        };
        reset();
        return reading;
    }

    void reset() noexcept {
        accumulated_frames_ = 0;
        peak_abs_ = 0.0f;
        clipped_ = false;
    }

private:
    void updateWindow(const float* samples, size_t sample_count) noexcept {
        for (size_t i = 0; i < sample_count; ++i) {
            const float magnitude = std::abs(samples[i]);
            if (!std::isfinite(magnitude)) {
                peak_abs_ = std::numeric_limits<float>::infinity();
                clipped_ = true;
                return;
            }

            peak_abs_ = std::max(peak_abs_, magnitude);
            clipped_ = clipped_ || magnitude > 1.0f;
        }
    }

    uint32_t window_frames_ = 1;
    uint32_t accumulated_frames_ = 0;
    float    peak_abs_ = 0.0f;
    bool     clipped_ = false;
};
