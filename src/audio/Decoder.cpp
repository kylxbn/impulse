#include "Decoder.hpp"
#include "ReplayGain.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/frame.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include "metadata/MetadataReader.hpp"

#include <cmath>
#include <format>
#include <ranges>

namespace {

struct FrameSkipSamples {
    int64_t start_skip_samples = 0;
    int64_t end_skip_samples = 0;
};

std::optional<FrameSkipSamples> readFrameSkipSamples(const AVFrame* frame) {
    if (!frame)
        return std::nullopt;

    const AVFrameSideData* side_data =
        av_frame_get_side_data(frame, AV_FRAME_DATA_SKIP_SAMPLES);
    if (!side_data || !side_data->data || side_data->size < 10)
        return std::nullopt;

    return FrameSkipSamples{
        .start_skip_samples = static_cast<int64_t>(AV_RL32(side_data->data)),
        .end_skip_samples = static_cast<int64_t>(AV_RL32(side_data->data + 4)),
    };
}

std::string toLower(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (unsigned char c : value)
        lowered.push_back(static_cast<char>(std::tolower(c)));
    return lowered;
}

std::string dictGetIgnoreCase(AVDictionary* dict, std::string_view key) {
    if (!dict)
        return {};

    const std::string lowered_key = toLower(key);
    AVDictionaryEntry* entry = nullptr;
    while ((entry = av_dict_get(dict, "", entry, AV_DICT_IGNORE_SUFFIX))) {
        if (!entry->key || !entry->value)
            continue;
        if (toLower(entry->key) == lowered_key)
            return entry->value;
    }
    return {};
}

void appendDictionaryFields(std::vector<TrackInfoField>& fields, AVDictionary* dict) {
    if (!dict)
        return;

    AVDictionaryEntry* entry = nullptr;
    while ((entry = av_dict_get(dict, "", entry, AV_DICT_IGNORE_SUFFIX))) {
        if (!entry->key || !entry->value)
            continue;
        fields.push_back(TrackInfoField{
            .label = entry->key,
            .value = entry->value,
        });
    }
}

std::string streamTitleFallback(AVDictionary* stream_dict, AVDictionary* format_dict) {
    std::string title = dictGetIgnoreCase(stream_dict, "StreamTitle");
    if (title.empty())
        title = dictGetIgnoreCase(format_dict, "StreamTitle");
    return title;
}

void parseLiveStreamText(AVDictionary* stream_dict,
                         AVDictionary* format_dict,
                         TrackInfo& info) {
    auto best_tag = [&](std::string_view key) {
        std::string value = dictGetIgnoreCase(stream_dict, key);
        if (value.empty())
            value = dictGetIgnoreCase(format_dict, key);
        return value;
    };

    info.title = best_tag("title");
    info.artist = best_tag("artist");
    info.album = best_tag("album");
    info.genre = best_tag("genre");
    info.comment = best_tag("comment");

    std::string stream_title = streamTitleFallback(stream_dict, format_dict);
    if (!stream_title.empty() && info.title.empty()) {
        const size_t separator = stream_title.find(" - ");
        if (separator != std::string::npos) {
            if (info.artist.empty())
                info.artist = stream_title.substr(0, separator);
            info.title = stream_title.substr(separator + 3);
        } else {
            info.title = std::move(stream_title);
        }
    }

    if (info.album.empty()) {
        info.album = best_tag("icy-name");
        if (info.album.empty())
            info.album = best_tag("service_name");
    }
}

std::string buildLiveMetadataSignature(const TrackInfo& info) {
    std::string signature;
    signature.reserve(256);
    signature += info.title;
    signature.push_back('\n');
    signature += info.artist;
    signature.push_back('\n');
    signature += info.album;
    signature.push_back('\n');
    for (const auto& field : info.stream_metadata) {
        signature += field.label;
        signature.push_back('=');
        signature += field.value;
        signature.push_back('\n');
    }
    signature.push_back('\n');
    for (const auto& field : info.format_metadata) {
        signature += field.label;
        signature.push_back('=');
        signature += field.value;
        signature.push_back('\n');
    }
    return signature;
}

}  // namespace

// ---------------------------------------------------------------------------
Decoder::Decoder() = default;

Decoder::~Decoder() {
    close();
}

// ---------------------------------------------------------------------------
Decoder::OpenResult Decoder::open(const MediaSource& source,
                                  int output_sample_rate) {
    close();
    const std::string input = source.string();
    AVDictionary* options = nullptr;
    if (source.isUrl()) {
        av_dict_set(&options, "reconnect", "1", 0);
        av_dict_set(&options, "reconnect_streamed", "1", 0);
        av_dict_set(&options, "reconnect_at_eof", "1", 0);
        av_dict_set(&options, "reconnect_on_network_error", "1", 0);
        av_dict_set(&options, "reconnect_delay_max", "2", 0);
        av_dict_set(&options, "reconnect_max_retries", "3", 0);
        av_dict_set(&options, "reconnect_delay_total_max", "8", 0);
    }

    // ---- Open container ----
    if (avformat_open_input(&fmt_ctx_, input.c_str(), nullptr, &options) < 0) {
        av_dict_free(&options);
        return {false, std::format("Cannot open: {}", input)};
    }
    av_dict_free(&options);

    if (avformat_find_stream_info(fmt_ctx_, nullptr) < 0) {
        close();
        return {false, std::format("No stream info: {}", input)};
    }

    // ---- Find best audio stream ----
    const AVCodec* codec = nullptr;
    audio_stream_idx_ = av_find_best_stream(
        fmt_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);

    if (audio_stream_idx_ < 0 || !codec) {
        close();
        return {false, "No audio stream found"};
    }

    AVStream* stream = fmt_ctx_->streams[audio_stream_idx_];

    // ---- Open codec ----
    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) { close(); return {false, "Cannot alloc codec context"}; }

    if (avcodec_parameters_to_context(codec_ctx_, stream->codecpar) < 0) {
        close();
        return {false, "Cannot copy codec parameters"};
    }

    manual_skip_export_enabled_ = enableManualSkipExport();
    if (avcodec_open2(codec_ctx_, codec, nullptr) < 0) {
        close();
        return {false, "Cannot open codec"};
    }

    // ---- Compute total frames ----
    out_fmt_.sample_rate = output_sample_rate;
    out_fmt_.channels    = 2;  // always stereo output
    out_fmt_.av_format   = AV_SAMPLE_FMT_FLT;

    if (source.isUrl()) {
        total_frames_ = 0;
    } else if (stream->duration != AV_NOPTS_VALUE) {
        double dur_s = static_cast<double>(stream->duration)
                     * av_q2d(stream->time_base);
        total_frames_ = static_cast<int64_t>(dur_s * output_sample_rate);
    } else if (fmt_ctx_->duration != AV_NOPTS_VALUE) {
        double dur_s  = static_cast<double>(fmt_ctx_->duration) / AV_TIME_BASE;
        total_frames_ = static_cast<int64_t>(dur_s * output_sample_rate);
    }

    // ---- Set up resampler ----
    if (!initResampler()) {
        close();
        return {false, "Cannot initialize resampler"};
    }

    // ---- Allocate packet/frame ----
    packet_ = av_packet_alloc();
    frame_  = av_frame_alloc();
    if (!packet_ || !frame_) {
        close();
        return {false, "Cannot allocate decode buffers"};
    }

    // ---- Read metadata ----
    auto meta = MetadataReader::read(source);
    if (meta) track_info_ = std::move(*meta);
    track_info_.source = source;
    track_info_.path = source.path;
    track_info_.is_stream = source.isUrl();
    track_info_.seekable = track_info_.seekable && !source.isUrl();
    replay_gain_scale_ = ReplayGain::linearGainForTrack(track_info_, replay_gain_settings_);
    gapless_trimmer_.reset(codec_ctx_->sample_rate,
                           output_sample_rate,
                           out_fmt_.channels,
                           stream->codecpar->initial_padding,
                           stream->codecpar->trailing_padding);
    track_info_.initial_padding_samples = stream->codecpar->initial_padding;
    track_info_.trailing_padding_samples = stream->codecpar->trailing_padding;
    track_info_.seek_preroll_samples = stream->codecpar->seek_preroll;
    track_info_.manual_skip_export_enabled = manual_skip_export_enabled_;
    if (!track_info_.is_stream)
        total_frames_ = gapless_trimmer_.adjustedTotalFrames(total_frames_);
    if (track_info_.is_stream) {
        total_frames_ = 0;
        track_info_.duration_seconds = 0.0;
        track_info_.finite_duration = false;
    } else if (total_frames_ > 0 && out_fmt_.sample_rate > 0) {
        track_info_.duration_seconds =
            static_cast<double>(total_frames_) / static_cast<double>(out_fmt_.sample_rate);
    }
    track_info_.ffmpeg_analysis.push_back(TrackInfoField{
        .label = "Trim mode",
        .value = manual_skip_export_enabled_
            ? "manual frame skip metadata"
            : "codec padding fallback",
    });
    track_info_.ffmpeg_analysis.push_back(TrackInfoField{
        .label = "Manual skip export",
        .value = manual_skip_export_enabled_ ? "enabled" : "unavailable",
    });
    track_info_.ffmpeg_analysis.push_back(TrackInfoField{
        .label = "Trimmed duration",
        .value = track_info_.duration_seconds > 0.0
            ? std::format("{:.6f} s", track_info_.duration_seconds)
            : "unknown",
    });
    input_eof_ = false;
    track_info_dirty_ = false;
    live_metadata_signature_ = buildLiveMetadataSignature(track_info_);

    return {true, {}};
}

Decoder::OpenResult Decoder::open(const std::filesystem::path& path,
                                  int output_sample_rate) {
    return open(MediaSource::fromPath(path), output_sample_rate);
}

// ---------------------------------------------------------------------------
void Decoder::close() {
    av_frame_free(&frame_);
    av_packet_free(&packet_);
    freeResampler();
    avcodec_free_context(&codec_ctx_);
    avformat_close_input(&fmt_ctx_);
    audio_stream_idx_ = -1;
    total_frames_     = 0;
    instantaneous_bitrate_bps_ = 0;
    replay_gain_scale_ = 1.0f;
    gapless_trimmer_.clear();
    manual_skip_export_enabled_ = false;
    input_eof_ = false;
    track_info_dirty_ = false;
    live_metadata_signature_.clear();
    track_info_       = {};
}

// ---------------------------------------------------------------------------
bool Decoder::initResampler() {
    freeResampler();
    if (!codec_ctx_) return false;

    AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(
        &swr_ctx_,
        &out_layout,               AV_SAMPLE_FMT_FLT, out_fmt_.sample_rate,
        &codec_ctx_->ch_layout,    codec_ctx_->sample_fmt, codec_ctx_->sample_rate,
        0, nullptr) < 0 || !swr_ctx_) {
        swr_ctx_ = nullptr;
        return false;
    }

    if (swr_init(swr_ctx_) < 0) {
        swr_free(&swr_ctx_);
        return false;
    }
    return true;
}

bool Decoder::enableManualSkipExport() {
    if (!codec_ctx_)
        return false;

    return av_opt_set(codec_ctx_, "skip_manual", "1", 0) >= 0;
}

void Decoder::freeResampler() {
    if (swr_ctx_) { swr_free(&swr_ctx_); swr_ctx_ = nullptr; }
}

// ---------------------------------------------------------------------------
int Decoder::decodeNextFrames(std::vector<float>& out_pcm) {
    if (!fmt_ctx_ || !codec_ctx_ || !swr_ctx_) return -1;

    out_pcm.clear();

    while (true) {
        // Try to receive a decoded frame from the codec
        int ret = avcodec_receive_frame(codec_ctx_, frame_);
        if (ret == 0) {
            const auto frame_skip_samples =
                manual_skip_export_enabled_ ? readFrameSkipSamples(frame_) : std::nullopt;

            // Got a frame, resample to f32 stereo
            int max_out = swr_get_out_samples(swr_ctx_, frame_->nb_samples);
            size_t offset = out_pcm.size();
            out_pcm.resize(offset + static_cast<size_t>(max_out * 2));

            float* out_ptr = out_pcm.data() + offset;
            uint8_t* dst[1] = { reinterpret_cast<uint8_t*>(out_ptr) };

            int frames_out = swr_convert(
                swr_ctx_,
                dst, max_out,
                const_cast<const uint8_t**>(frame_->extended_data),
                frame_->nb_samples);

            if (frames_out < 0) {
                out_pcm.resize(offset);
                return -1;
            }

            // Trim to actual output
            out_pcm.resize(offset + static_cast<size_t>(frames_out * 2));
            std::vector<float> frame_pcm(out_pcm.begin() + static_cast<std::ptrdiff_t>(offset),
                                         out_pcm.end());
            out_pcm.resize(offset);

            if (frame_skip_samples) {
                gapless_trimmer_.applyExplicitFrameTrim(frame_pcm,
                                                        codec_ctx_->sample_rate,
                                                        frame_skip_samples->start_skip_samples,
                                                        frame_skip_samples->end_skip_samples);
            } else if (!manual_skip_export_enabled_) {
                gapless_trimmer_.applyFallbackTrim(frame_pcm, false);
            }

            if (!frame_pcm.empty()) {
                ReplayGain::apply(std::span<float>(frame_pcm.data(), frame_pcm.size()),
                                  replay_gain_scale_);
                out_pcm.insert(out_pcm.end(), frame_pcm.begin(), frame_pcm.end());
            }
            av_frame_unref(frame_);

            if (!out_pcm.empty()) return static_cast<int>(out_pcm.size() / 2);
            continue;
        }

        if (ret == AVERROR(EAGAIN)) {
            // Need more packets
            ret = av_read_frame(fmt_ctx_, packet_);
            if (ret == AVERROR_EOF) {
                if (!input_eof_) {
                    input_eof_ = true;
                    if (avcodec_send_packet(codec_ctx_, nullptr) < 0)
                        return -1;
                    continue;
                }

                return 0;
            }
            if (ret < 0) return -1;

            refreshLiveTrackInfo();

            if (packet_->stream_index == audio_stream_idx_) {
                AVStream* stream = fmt_ctx_->streams[audio_stream_idx_];
                double packet_duration_seconds = 0.0;
                if (packet_->duration > 0)
                    packet_duration_seconds = packet_->duration * av_q2d(stream->time_base);
                if (packet_duration_seconds > 0.0)
                    instantaneous_bitrate_bps_ = static_cast<int64_t>(
                        (static_cast<double>(packet_->size) * 8.0) / packet_duration_seconds);
                else
                    instantaneous_bitrate_bps_ = track_info_.bitrate_bps;
                if (avcodec_send_packet(codec_ctx_, packet_) < 0) {
                    av_packet_unref(packet_);
                    return -1;
                }
            }
            av_packet_unref(packet_);
            continue;
        }

        if (ret == AVERROR_EOF) {
            const size_t offset = out_pcm.size();
            const int max_flush = swr_get_out_samples(swr_ctx_, 0);
            if (max_flush > 0) {
                out_pcm.resize(offset + static_cast<size_t>(max_flush * 2));
                uint8_t* dst_flush[1] = {
                    reinterpret_cast<uint8_t*>(out_pcm.data() + offset)
                };
                const int flushed_frames = swr_convert(swr_ctx_, dst_flush, max_flush, nullptr, 0);
                if (flushed_frames < 0) {
                    out_pcm.resize(offset);
                    return -1;
                }

                out_pcm.resize(offset + static_cast<size_t>(flushed_frames * 2));
                std::vector<float> flush_pcm(out_pcm.begin() + static_cast<std::ptrdiff_t>(offset),
                                             out_pcm.end());
                out_pcm.resize(offset);

                if (!manual_skip_export_enabled_)
                    gapless_trimmer_.applyFallbackTrim(flush_pcm, true);

                if (!flush_pcm.empty()) {
                    ReplayGain::apply(std::span<float>(flush_pcm.data(), flush_pcm.size()),
                                      replay_gain_scale_);
                    out_pcm.insert(out_pcm.end(), flush_pcm.begin(), flush_pcm.end());
                }

                if (!out_pcm.empty())
                    return static_cast<int>(out_pcm.size() / 2);
            }

            return 0;  // codec fully flushed
        }
        return -1;                          // real error
    }
}

// ---------------------------------------------------------------------------
bool Decoder::seek(double position_seconds) {
    if (!fmt_ctx_ || !codec_ctx_ || audio_stream_idx_ < 0 || !track_info_.seekable) return false;

    AVStream* stream = fmt_ctx_->streams[audio_stream_idx_];
    const double raw_seek_seconds = gapless_trimmer_.rawSeekSeconds(position_seconds);
    int64_t ts = static_cast<int64_t>(raw_seek_seconds / av_q2d(stream->time_base));

    int ret = avformat_seek_file(fmt_ctx_, audio_stream_idx_,
                                 INT64_MIN, ts, ts, 0);
    if (ret < 0) return false;

    avcodec_flush_buffers(codec_ctx_);
    freeResampler();
    if (!initResampler())
        return false;
    gapless_trimmer_.resetForSeek(position_seconds);
    input_eof_ = false;
    return true;
}

void Decoder::setReplayGainSettings(ReplayGain::ReplayGainSettings settings) {
    replay_gain_settings_ = settings;
    replay_gain_scale_ = ReplayGain::linearGainForTrack(track_info_, replay_gain_settings_);
}

std::optional<TrackInfo> Decoder::consumeTrackInfoUpdate() {
    if (!track_info_dirty_)
        return std::nullopt;

    track_info_dirty_ = false;
    return track_info_;
}

bool Decoder::refreshLiveTrackInfo() {
    if (!fmt_ctx_ || audio_stream_idx_ < 0 || !track_info_.is_stream)
        return false;

    AVStream* stream = fmt_ctx_->streams[audio_stream_idx_];
    TrackInfo updated = track_info_;
    updated.stream_metadata.clear();
    updated.format_metadata.clear();
    appendDictionaryFields(updated.stream_metadata, stream ? stream->metadata : nullptr);
    appendDictionaryFields(updated.format_metadata, fmt_ctx_->metadata);
    parseLiveStreamText(stream ? stream->metadata : nullptr, fmt_ctx_->metadata, updated);

    const std::string signature = buildLiveMetadataSignature(updated);
    if (signature == live_metadata_signature_)
        return false;

    track_info_ = std::move(updated);
    live_metadata_signature_ = signature;
    replay_gain_scale_ = ReplayGain::linearGainForTrack(track_info_, replay_gain_settings_);
    track_info_dirty_ = true;
    return true;
}
