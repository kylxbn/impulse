#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

class GaplessTrimmer {
public:
    void reset(int input_sample_rate,
               int output_sample_rate,
               int channels,
               int64_t initial_padding_samples,
               int64_t trailing_padding_samples) {
        input_sample_rate_ = input_sample_rate;
        output_sample_rate_ = output_sample_rate;
        channels_ = std::max(channels, 0);
        initial_padding_samples_ = std::max<int64_t>(initial_padding_samples, 0);
        trailing_padding_samples_ = std::max<int64_t>(trailing_padding_samples, 0);
        fallback_leading_frames_remaining_ =
            rescaleSamples(initial_padding_samples_, input_sample_rate_, output_sample_rate_);
        fallback_trailing_frames_ =
            rescaleSamples(trailing_padding_samples_, input_sample_rate_, output_sample_rate_);
        fallback_tail_pcm_.clear();
    }

    void clear() noexcept {
        input_sample_rate_ = 0;
        output_sample_rate_ = 0;
        channels_ = 0;
        initial_padding_samples_ = 0;
        trailing_padding_samples_ = 0;
        fallback_leading_frames_remaining_ = 0;
        fallback_trailing_frames_ = 0;
        fallback_tail_pcm_.clear();
    }

    void resetForSeek(double position_seconds) {
        fallback_leading_frames_remaining_ =
            position_seconds <= 0.0
                ? rescaleSamples(initial_padding_samples_, input_sample_rate_, output_sample_rate_)
                : 0;
        fallback_tail_pcm_.clear();
    }

    double rawSeekSeconds(double trimmed_position_seconds) const {
        if (trimmed_position_seconds <= 0.0)
            return 0.0;

        return trimmed_position_seconds + leadingTrimSeconds();
    }

    int64_t adjustedTotalFrames(int64_t raw_total_frames) const {
        if (raw_total_frames <= 0)
            return 0;

        const int64_t trim_frames =
            rescaleSamples(initial_padding_samples_, input_sample_rate_, output_sample_rate_) +
            rescaleSamples(trailing_padding_samples_, input_sample_rate_, output_sample_rate_);
        return std::max<int64_t>(0, raw_total_frames - trim_frames);
    }

    void applyExplicitFrameTrim(std::vector<float>& pcm,
                                int frame_sample_rate,
                                int64_t start_skip_samples,
                                int64_t end_skip_samples) const {
        const int effective_sample_rate = frame_sample_rate > 0
            ? frame_sample_rate
            : input_sample_rate_;
        const int64_t start_skip_frames =
            rescaleSamples(start_skip_samples, effective_sample_rate, output_sample_rate_);
        const int64_t end_skip_frames =
            rescaleSamples(end_skip_samples, effective_sample_rate, output_sample_rate_);
        trimFrames(pcm, start_skip_frames, end_skip_frames);
    }

    void applyFallbackTrim(std::vector<float>& pcm, bool end_of_stream) {
        trimLeadingFallback(pcm);

        if (fallback_trailing_frames_ <= 0 || channels_ <= 0)
            return;

        fallback_tail_pcm_.insert(fallback_tail_pcm_.end(), pcm.begin(), pcm.end());
        pcm.clear();

        const size_t keep_samples =
            static_cast<size_t>(fallback_trailing_frames_) * static_cast<size_t>(channels_);
        const size_t releasable_samples = fallback_tail_pcm_.size() > keep_samples
            ? fallback_tail_pcm_.size() - keep_samples
            : 0;

        if (end_of_stream) {
            if (releasable_samples > 0) {
                pcm.assign(fallback_tail_pcm_.begin(),
                           fallback_tail_pcm_.begin() +
                               static_cast<std::ptrdiff_t>(releasable_samples));
            }
            fallback_tail_pcm_.clear();
            return;
        }

        if (releasable_samples == 0)
            return;

        pcm.assign(fallback_tail_pcm_.begin(),
                   fallback_tail_pcm_.begin() +
                       static_cast<std::ptrdiff_t>(releasable_samples));
        fallback_tail_pcm_.erase(
            fallback_tail_pcm_.begin(),
            fallback_tail_pcm_.begin() + static_cast<std::ptrdiff_t>(releasable_samples));
    }

    double leadingTrimSeconds() const {
        if (input_sample_rate_ > 0)
            return static_cast<double>(initial_padding_samples_) /
                   static_cast<double>(input_sample_rate_);
        if (output_sample_rate_ > 0)
            return static_cast<double>(
                       rescaleSamples(initial_padding_samples_, input_sample_rate_, output_sample_rate_)) /
                   static_cast<double>(output_sample_rate_);
        return 0.0;
    }

    static int64_t rescaleSamples(int64_t samples, int input_sample_rate, int output_sample_rate) {
        if (samples <= 0 || input_sample_rate <= 0 || output_sample_rate <= 0)
            return 0;

        return std::max<int64_t>(
            0,
            static_cast<int64_t>(std::llround(
                static_cast<long double>(samples) * output_sample_rate / input_sample_rate)));
    }

private:
    void trimLeadingFallback(std::vector<float>& pcm) {
        if (fallback_leading_frames_remaining_ <= 0 || channels_ <= 0 || pcm.empty())
            return;

        const int64_t available_frames =
            static_cast<int64_t>(pcm.size() / static_cast<size_t>(channels_));
        const int64_t frames_to_trim =
            std::min(fallback_leading_frames_remaining_, available_frames);
        trimFrames(pcm, frames_to_trim, 0);
        fallback_leading_frames_remaining_ -= frames_to_trim;
    }

    void trimFrames(std::vector<float>& pcm,
                    int64_t leading_frames,
                    int64_t trailing_frames) const {
        if (channels_ <= 0 || pcm.empty())
            return;

        const int64_t total_frames =
            static_cast<int64_t>(pcm.size() / static_cast<size_t>(channels_));
        const int64_t clamped_leading = std::clamp<int64_t>(leading_frames, 0, total_frames);
        const int64_t max_trailing = std::max<int64_t>(0, total_frames - clamped_leading);
        const int64_t clamped_trailing = std::clamp<int64_t>(trailing_frames, 0, max_trailing);
        const size_t start_sample =
            static_cast<size_t>(clamped_leading) * static_cast<size_t>(channels_);
        const size_t kept_frames =
            static_cast<size_t>(total_frames - clamped_leading - clamped_trailing);
        const size_t kept_samples = kept_frames * static_cast<size_t>(channels_);

        if (kept_samples == 0) {
            pcm.clear();
            return;
        }

        if (start_sample > 0) {
            std::move(pcm.begin() + static_cast<std::ptrdiff_t>(start_sample),
                      pcm.begin() + static_cast<std::ptrdiff_t>(start_sample + kept_samples),
                      pcm.begin());
        }
        pcm.resize(kept_samples);
    }

    int input_sample_rate_ = 0;
    int output_sample_rate_ = 0;
    int channels_ = 0;
    int64_t initial_padding_samples_ = 0;
    int64_t trailing_padding_samples_ = 0;
    int64_t fallback_leading_frames_remaining_ = 0;
    int64_t fallback_trailing_frames_ = 0;
    std::vector<float> fallback_tail_pcm_;
};
