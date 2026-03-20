#pragma once

#include "audio/DecoderBackend.hpp"
#include "audio/DecoderProvider.hpp"

#include <memory>
#include <vector>

namespace openmpt {
class module;
}

class OpenMptDecoderBackend final : public DecoderBackend {
public:
    OpenMptDecoderBackend();
    ~OpenMptDecoderBackend() override;

    OpenMptDecoderBackend(const OpenMptDecoderBackend&) = delete;
    OpenMptDecoderBackend& operator=(const OpenMptDecoderBackend&) = delete;

    OpenResult open(const MediaSource& source,
                    int output_sample_rate) override;
    void close() override;
    [[nodiscard]] bool isOpen() const override;
    int decodeNextFrames(std::vector<float>& out_pcm) override;
    std::optional<TrackInfo> consumeTrackInfoUpdate() override;
    bool seek(double position_seconds) override;
    void setReplayGainSettings(ReplayGain::ReplayGainSettings settings) override;

    [[nodiscard]] const AudioFormat& outputFormat() const override { return out_fmt_; }
    [[nodiscard]] const TrackInfo& trackInfo() const override { return track_info_; }
    [[nodiscard]] int64_t totalFrames() const override { return total_frames_; }
    [[nodiscard]] int64_t instantaneousBitrateBps() const override {
        return estimated_bitrate_bps_;
    }

private:
    static constexpr size_t kRenderChunkFrames = 2048;

    std::unique_ptr<openmpt::module> module_;
    AudioFormat out_fmt_;
    TrackInfo track_info_;
    int64_t total_frames_ = 0;
    int64_t estimated_bitrate_bps_ = 0;
    float replay_gain_scale_ = 1.0f;
    ReplayGain::ReplayGainSettings replay_gain_settings_;
};

[[nodiscard]] const DecoderProvider& openMptDecoderProvider();
