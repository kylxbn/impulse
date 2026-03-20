#pragma once

#include "audio/DecoderBackend.hpp"
#include "audio/DecoderProvider.hpp"

#include <vector>

struct _data_loader;
struct _waveform_32bit_stereo;
using DATA_LOADER = _data_loader;
using WAVE_32BS = _waveform_32bit_stereo;

class VGMPlayer;

class VgmDecoderBackend final : public DecoderBackend {
public:
    VgmDecoderBackend();
    ~VgmDecoderBackend() override;

    VgmDecoderBackend(const VgmDecoderBackend&) = delete;
    VgmDecoderBackend& operator=(const VgmDecoderBackend&) = delete;

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

    DATA_LOADER* loader_ = nullptr;
    std::unique_ptr<VGMPlayer> player_;
    AudioFormat out_fmt_;
    TrackInfo track_info_;
    int64_t total_frames_ = 0;
    int64_t estimated_bitrate_bps_ = 0;
    float replay_gain_scale_ = 1.0f;
    ReplayGain::ReplayGainSettings replay_gain_settings_;
    std::vector<WAVE_32BS> render_buffer_;
};

[[nodiscard]] const DecoderProvider& vgmDecoderProvider();
