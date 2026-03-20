#pragma once

#include "audio/DecoderBackend.hpp"
#include "audio/DecoderProvider.hpp"
#include "audio/GaplessTrimmer.hpp"

#include <string>

struct AVFormatContext;
struct AVCodecContext;
struct AVPacket;
struct AVFrame;
struct SwrContext;

class FFmpegDecoderBackend final : public DecoderBackend {
public:
    FFmpegDecoderBackend();
    ~FFmpegDecoderBackend() override;

    FFmpegDecoderBackend(const FFmpegDecoderBackend&) = delete;
    FFmpegDecoderBackend& operator=(const FFmpegDecoderBackend&) = delete;

    OpenResult open(const MediaSource& source,
                    int output_sample_rate) override;
    void close() override;
    [[nodiscard]] bool isOpen() const override { return fmt_ctx_ != nullptr; }
    int decodeNextFrames(std::vector<float>& out_pcm) override;
    std::optional<TrackInfo> consumeTrackInfoUpdate() override;
    bool seek(double position_seconds) override;
    void setReplayGainSettings(ReplayGain::ReplayGainSettings settings) override;

    [[nodiscard]] const AudioFormat& outputFormat() const override { return out_fmt_; }
    [[nodiscard]] const TrackInfo& trackInfo() const override { return track_info_; }
    [[nodiscard]] int64_t totalFrames() const override { return total_frames_; }
    [[nodiscard]] int64_t instantaneousBitrateBps() const override {
        return instantaneous_bitrate_bps_;
    }

private:
    bool initResampler();
    void freeResampler();
    bool enableManualSkipExport();
    bool refreshLiveTrackInfo();

    AVFormatContext* fmt_ctx_ = nullptr;
    AVCodecContext*  codec_ctx_ = nullptr;
    SwrContext*      swr_ctx_ = nullptr;
    AVPacket*        packet_ = nullptr;
    AVFrame*         frame_ = nullptr;

    int     audio_stream_idx_ = -1;
    int64_t total_frames_ = 0;
    int64_t instantaneous_bitrate_bps_ = 0;
    float   replay_gain_scale_ = 1.0f;
    ReplayGain::ReplayGainSettings replay_gain_settings_;
    GaplessTrimmer gapless_trimmer_;
    bool    manual_skip_export_enabled_ = false;
    bool    input_eof_ = false;
    bool    track_info_dirty_ = false;
    std::string live_metadata_signature_;

    bool resampleInto(std::vector<float>& buf,
                      const uint8_t* const* input_data,
                      int input_samples);

    AudioFormat        out_fmt_;
    TrackInfo          track_info_;
    std::vector<float> resample_buf_;
};

[[nodiscard]] const DecoderProvider& ffmpegDecoderProvider();
