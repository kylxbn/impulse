#pragma once

#include "audio/GaplessTrimmer.hpp"
#include "audio/ReplayGain.hpp"
#include "core/AudioFormat.hpp"
#include "core/MediaSource.hpp"
#include "core/TrackInfo.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct AVPacket;
struct AVFrame;
struct SwrContext;

// Decodes an audio file frame-by-frame.
// Always resamples/converts output to f32 interleaved (AV_SAMPLE_FMT_FLT)
// at the requested output sample rate.
//
// NOT thread-safe: own and use this exclusively from the decode thread.
class Decoder {
public:
    Decoder();
    ~Decoder();

    // Non-copyable, non-movable (holds raw C pointers)
    Decoder(const Decoder&)            = delete;
    Decoder& operator=(const Decoder&) = delete;

    struct OpenResult {
        bool        ok    = false;
        std::string error;
    };

    // Open a file.  Closes any previously open file first.
    // output_sample_rate: the target rate after resampling (e.g. 48000).
    OpenResult open(const MediaSource& source,
                    int output_sample_rate = 48000);
    OpenResult open(const std::filesystem::path& path,
                    int output_sample_rate = 48000);

    // Close the current file and free all resources.
    void close();

    bool is_open() const { return fmt_ctx_ != nullptr; }

    // Decode the next chunk of audio into out_pcm (f32 interleaved).
    // Returns number of frames written, 0 at end-of-stream, or -1 on error.
    int decodeNextFrames(std::vector<float>& out_pcm);
    std::optional<TrackInfo> consumeTrackInfoUpdate();

    // Seek to the given position (seconds).
    // Returns true on success.  Caller must call decodeNextFrames() again.
    bool seek(double position_seconds);
    void setReplayGainSettings(ReplayGain::ReplayGainSettings settings);

    const AudioFormat&     outputFormat() const { return out_fmt_; }
    const TrackInfo&       trackInfo()    const { return track_info_; }
    int64_t                totalFrames()  const { return total_frames_; }
    int64_t                instantaneousBitrateBps() const { return instantaneous_bitrate_bps_; }

private:
    bool initResampler();
    void freeResampler();
    bool enableManualSkipExport();
    bool refreshLiveTrackInfo();

    AVFormatContext* fmt_ctx_   = nullptr;
    AVCodecContext*  codec_ctx_ = nullptr;
    SwrContext*      swr_ctx_   = nullptr;
    AVPacket*        packet_    = nullptr;
    AVFrame*         frame_     = nullptr;

    int     audio_stream_idx_ = -1;
    int64_t total_frames_     = 0;
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
