#include "FFmpegMetadataReader.hpp"

#include "core/Lyrics.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstring>
#include <format>
#include <string_view>
#include <tuple>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Look up a tag (case-insensitive) from an AVDictionary.
// Returns empty string_view if not found.
std::string dictGet(AVDictionary* dict, const char* key) {
    if (!dict) return {};
    AVDictionaryEntry* e = av_dict_get(dict, key, nullptr, AV_DICT_IGNORE_SUFFIX);
    if (!e || !e->value) return {};
    return e->value;
}

// Parse a ReplayGain value string like "-6.50 dB" or "0.947998" into float.
std::optional<float> parseRGFloat(const std::string& s) {
    if (s.empty()) return std::nullopt;
    float val{};
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
    if (ec != std::errc{}) return std::nullopt;
    return val;
}

void appendField(std::vector<TrackInfoField>& fields,
                 std::string label,
                 std::string value) {
    if (value.empty())
        return;

    fields.push_back(TrackInfoField{
        .label = std::move(label),
        .value = std::move(value),
    });
}

void appendIntegerField(std::vector<TrackInfoField>& fields,
                        std::string label,
                        int64_t value,
                        bool allow_zero = false) {
    if (!allow_zero && value == 0)
        return;

    appendField(fields, std::move(label), std::to_string(value));
}

void appendDictionaryFields(std::vector<TrackInfoField>& fields,
                            AVDictionary* dict) {
    if (!dict)
        return;

    AVDictionaryEntry* entry = nullptr;
    while ((entry = av_dict_get(dict, "", entry, AV_DICT_IGNORE_SUFFIX))) {
        if (!entry->key || !entry->value)
            continue;
        appendField(fields, entry->key, entry->value);
    }
}

std::string toLower(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (unsigned char ch : value)
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    return lowered;
}

std::string dictGetCaseInsensitive(AVDictionary* dict, std::string_view key) {
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

int albumArtNamePriority(std::string_view stem) {
    static constexpr std::array<std::string_view, 4> kNames = {
        "cover",
        "front",
        "folder",
        "album",
    };

    const auto it = std::find(kNames.begin(), kNames.end(), stem);
    if (it == kNames.end())
        return -1;
    return static_cast<int>(std::distance(kNames.begin(), it));
}

int albumArtExtensionPriority(std::string_view extension) {
    static constexpr std::array<std::string_view, 4> kExtensions = {
        "png",
        "webp",
        "jpg",
        "jpeg",
    };

    const auto it = std::find(kExtensions.begin(), kExtensions.end(), extension);
    if (it == kExtensions.end())
        return -1;
    return static_cast<int>(std::distance(kExtensions.begin(), it));
}

AVPixelFormat normalizedAlbumArtPixelFormat(AVPixelFormat format) {
    switch (format) {
    case AV_PIX_FMT_YUVJ420P: return AV_PIX_FMT_YUV420P;
    case AV_PIX_FMT_YUVJ422P: return AV_PIX_FMT_YUV422P;
    case AV_PIX_FMT_YUVJ444P: return AV_PIX_FMT_YUV444P;
    case AV_PIX_FMT_YUVJ440P: return AV_PIX_FMT_YUV440P;
    case AV_PIX_FMT_YUVJ411P: return AV_PIX_FMT_YUV411P;
    default: return format;
    }
}

bool isFullRangeAlbumArtPixelFormat(AVPixelFormat format) {
    switch (format) {
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUVJ422P:
    case AV_PIX_FMT_YUVJ444P:
    case AV_PIX_FMT_YUVJ440P:
    case AV_PIX_FMT_YUVJ411P:
        return true;
    default:
        return false;
    }
}

bool decodeFrameToAlbumArt(AVFrame* frame, TrackInfo& info) {
    if (!frame || frame->width <= 0 || frame->height <= 0)
        return false;

    const int w = frame->width;
    const int h = frame->height;
    const auto source_format = static_cast<AVPixelFormat>(frame->format);
    const auto normalized_format = normalizedAlbumArtPixelFormat(source_format);
    SwsContext* sws = sws_getContext(
        w, h, normalized_format,
        w, h, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!sws)
        return false;

    const int src_range =
        frame->color_range == AVCOL_RANGE_JPEG || isFullRangeAlbumArtPixelFormat(source_format);
    const int* coeffs = sws_getCoefficients(SWS_CS_DEFAULT);
    sws_setColorspaceDetails(sws,
                             coeffs, src_range,
                             coeffs, 1,
                             0, 1 << 16, 1 << 16);

    AVFrame* rgba_frame = av_frame_alloc();
    if (!rgba_frame) {
        sws_freeContext(sws);
        return false;
    }

    rgba_frame->format = AV_PIX_FMT_RGBA;
    rgba_frame->width = w;
    rgba_frame->height = h;

    if (av_frame_get_buffer(rgba_frame, 0) < 0) {
        av_frame_free(&rgba_frame);
        sws_freeContext(sws);
        return false;
    }

    if (av_frame_make_writable(rgba_frame) < 0) {
        av_frame_free(&rgba_frame);
        sws_freeContext(sws);
        return false;
    }

    const int scaled_rows = sws_scale_frame(sws, rgba_frame, frame);
    sws_freeContext(sws);
    if (scaled_rows < 0) {
        av_frame_free(&rgba_frame);
        return false;
    }

    std::vector<uint8_t> rgba(static_cast<size_t>(w * h * 4));
    const size_t row_bytes = static_cast<size_t>(w) * 4;
    for (int y = 0; y < h; ++y) {
        std::memcpy(rgba.data() + static_cast<size_t>(y) * row_bytes,
                    rgba_frame->data[0] + static_cast<size_t>(y) * rgba_frame->linesize[0],
                    row_bytes);
    }
    av_frame_free(&rgba_frame);

    info.album_art_rgba = std::move(rgba);
    info.album_art_width = w;
    info.album_art_height = h;
    return true;
}

struct AlbumArtCandidate {
    std::filesystem::path path;
    int                   name_priority = 0;
    int                   extension_priority = 0;
    std::string           sort_key;
};

std::vector<AlbumArtCandidate>
findExternalAlbumArtCandidates(const std::filesystem::path& track_path) {
    std::vector<AlbumArtCandidate> candidates;

    std::error_code ec;
    const std::filesystem::path directory = track_path.parent_path();
    if (directory.empty() || !std::filesystem::is_directory(directory, ec))
        return candidates;

    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec)
            break;
        std::error_code entry_ec;
        if (!entry.is_regular_file(entry_ec) || entry_ec)
            continue;

        const auto path = entry.path();
        const std::string stem = toLower(path.stem().string());
        std::string extension = toLower(path.extension().string());
        if (!extension.empty() && extension.front() == '.')
            extension.erase(extension.begin());

        const int name_priority = albumArtNamePriority(stem);
        const int extension_priority = albumArtExtensionPriority(extension);
        if (name_priority < 0 || extension_priority < 0)
            continue;

        candidates.push_back(AlbumArtCandidate{
            .path = path,
            .name_priority = name_priority,
            .extension_priority = extension_priority,
            .sort_key = toLower(path.filename().string()),
        });
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const AlbumArtCandidate& lhs, const AlbumArtCandidate& rhs) {
                  return std::tie(lhs.name_priority, lhs.extension_priority, lhs.sort_key) <
                         std::tie(rhs.name_priority, rhs.extension_priority, rhs.sort_key);
              });
    return candidates;
}

bool decodeImageFileToAlbumArt(const std::filesystem::path& image_path, TrackInfo& info) {
    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, image_path.c_str(), nullptr, nullptr) < 0)
        return false;

    bool success = false;

    if (avformat_find_stream_info(fmt_ctx, nullptr) >= 0) {
        const int stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (stream_index >= 0) {
            AVStream* stream = fmt_ctx->streams[stream_index];
            const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
            if (codec) {
                AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
                if (codec_ctx) {
                    if (avcodec_parameters_to_context(codec_ctx, stream->codecpar) == 0 &&
                        avcodec_open2(codec_ctx, codec, nullptr) == 0) {
                        AVPacket* packet = av_packet_alloc();
                        AVFrame* frame = av_frame_alloc();
                        if (packet && frame) {
                            while (!success && av_read_frame(fmt_ctx, packet) >= 0) {
                                if (packet->stream_index == stream_index &&
                                    avcodec_send_packet(codec_ctx, packet) == 0) {
                                    while (!success) {
                                        const int receive_result = avcodec_receive_frame(codec_ctx, frame);
                                        if (receive_result == AVERROR(EAGAIN) ||
                                            receive_result == AVERROR_EOF)
                                            break;
                                        if (receive_result < 0)
                                            break;
                                        success = decodeFrameToAlbumArt(frame, info);
                                        av_frame_unref(frame);
                                    }
                                }
                                av_packet_unref(packet);
                            }

                            if (!success && avcodec_send_packet(codec_ctx, nullptr) == 0) {
                                while (!success) {
                                    const int receive_result = avcodec_receive_frame(codec_ctx, frame);
                                    if (receive_result == AVERROR(EAGAIN) ||
                                        receive_result == AVERROR_EOF)
                                        break;
                                    if (receive_result < 0)
                                        break;
                                    success = decodeFrameToAlbumArt(frame, info);
                                    av_frame_unref(frame);
                                }
                            }
                        }

                        if (frame)
                            av_frame_free(&frame);
                        if (packet)
                            av_packet_free(&packet);
                    }
                    avcodec_free_context(&codec_ctx);
                }
            }
        }
    }

    avformat_close_input(&fmt_ctx);
    return success;
}

std::optional<std::filesystem::path>
decodeExternalAlbumArt(const std::filesystem::path& track_path, TrackInfo& info) {
    for (const auto& candidate : findExternalAlbumArtCandidates(track_path)) {
        if (decodeImageFileToAlbumArt(candidate.path, info))
            return candidate.path;
    }
    return std::nullopt;
}

std::string readEmbeddedLyrics(AVDictionary* stream_dict,
                               AVDictionary* format_dict) {
    std::string lyrics = dictGetCaseInsensitive(stream_dict, "lyrics");
    if (lyrics.empty())
        lyrics = dictGetCaseInsensitive(format_dict, "lyrics");
    return lyrics;
}

} // namespace

// ---------------------------------------------------------------------------
// FFmpegMetadataReader::read
// ---------------------------------------------------------------------------

std::expected<TrackInfo, std::string>
FFmpegMetadataReader::read(const MediaSource& source,
                           MetadataReadOptions options) {
    AVFormatContext* fmt_ctx = nullptr;
    const std::string input = source.string();

    if (avformat_open_input(&fmt_ctx, input.c_str(), nullptr, nullptr) < 0)
        return std::unexpected(std::format("Cannot open file: {}", input));

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        avformat_close_input(&fmt_ctx);
        return std::unexpected(std::format("Cannot read stream info: {}", input));
    }

    TrackInfo info;
    info.source          = source;
    info.decoder_name    = "FFmpeg";
    info.path            = source.path;
    info.is_stream       = source.isUrl();
    info.seekable        = fmt_ctx->pb && (fmt_ctx->pb->seekable & AVIO_SEEKABLE_NORMAL);
    if (source.isFile()) {
        std::error_code file_size_ec;
        info.file_size_bytes = static_cast<int64_t>(std::filesystem::file_size(source.path, file_size_ec));
        if (file_size_ec)
            info.file_size_bytes = 0;
    }
    info.duration_seconds = (fmt_ctx->duration > 0)
        ? static_cast<double>(fmt_ctx->duration) / AV_TIME_BASE
        : 0.0;
    info.finite_duration = info.duration_seconds > 0.0;
    info.bitrate_bps     = fmt_ctx->bit_rate;
    appendField(info.decoder_analysis, "Source", input);
    appendIntegerField(info.decoder_analysis, "File size (bytes)", info.file_size_bytes);
    appendField(info.decoder_analysis, "Container duration",
                info.duration_seconds > 0.0 ? std::format("{:.6f} s", info.duration_seconds) : "");
    appendIntegerField(info.decoder_analysis, "Container bitrate (bps)", fmt_ctx->bit_rate);

    // Container format name
    if (fmt_ctx->iformat && fmt_ctx->iformat->name) {
        info.container_format = fmt_ctx->iformat->name;
        appendField(info.decoder_analysis, "Container short name", fmt_ctx->iformat->name);
        if (fmt_ctx->iformat->long_name)
            appendField(info.decoder_analysis, "Container long name", fmt_ctx->iformat->long_name);
    }
    appendIntegerField(info.decoder_analysis, "Stream count", fmt_ctx->nb_streams, true);

    // Find the best audio stream
    int audio_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio_idx >= 0) {
        AVStream*    stream = fmt_ctx->streams[audio_idx];
        AVCodecParameters* par = stream->codecpar;
        info.initial_padding_samples = par->initial_padding;
        info.trailing_padding_samples = par->trailing_padding;
        info.seek_preroll_samples = par->seek_preroll;

        // Codec name
        const AVCodec* codec = avcodec_find_decoder(par->codec_id);
        if (codec) {
            info.codec_name = codec->name;
            appendField(info.decoder_analysis, "Codec short name", codec->name);
            if (codec->long_name)
                appendField(info.decoder_analysis, "Codec long name", codec->long_name);
        }
        if (const AVCodecDescriptor* descriptor = avcodec_descriptor_get(par->codec_id)) {
            appendField(info.decoder_analysis, "Codec descriptor", descriptor->name);
            if (descriptor->long_name)
                appendField(info.decoder_analysis, "Codec description", descriptor->long_name);
        }

        // Technical fields
        info.sample_rate = par->sample_rate;
        info.channels    = par->ch_layout.nb_channels;
        info.bit_depth   = par->bits_per_raw_sample > 0
            ? par->bits_per_raw_sample
            : par->bits_per_coded_sample;
        if (par->bit_rate > 0)
            info.bitrate_bps = par->bit_rate;  // prefer stream-level bitrate
        appendIntegerField(info.decoder_analysis, "Audio stream index", audio_idx, true);
        appendIntegerField(info.decoder_analysis, "Audio stream id", stream->id);
        appendIntegerField(info.decoder_analysis, "Stream sample rate", par->sample_rate);
        appendIntegerField(info.decoder_analysis, "Channels", par->ch_layout.nb_channels);
        appendIntegerField(info.decoder_analysis, "Bits per raw sample", par->bits_per_raw_sample);
        appendIntegerField(info.decoder_analysis, "Bits per coded sample", par->bits_per_coded_sample);
        appendIntegerField(info.decoder_analysis, "Stream bitrate (bps)", par->bit_rate);
        appendIntegerField(info.decoder_analysis, "Frame size", par->frame_size);
        appendIntegerField(info.decoder_analysis, "Block align", par->block_align);
        appendIntegerField(info.decoder_analysis, "Initial padding (samples)", par->initial_padding, true);
        appendIntegerField(info.decoder_analysis, "Trailing padding (samples)", par->trailing_padding, true);
        appendIntegerField(info.decoder_analysis, "Seek preroll (samples)", par->seek_preroll, true);
        appendIntegerField(info.decoder_analysis, "Extradata size (bytes)", par->extradata_size);
        appendIntegerField(info.decoder_analysis, "Profile", par->profile);
        if (stream->duration != AV_NOPTS_VALUE) {
            appendIntegerField(info.decoder_analysis, "Stream duration (ticks)", stream->duration, true);
            appendField(info.decoder_analysis, "Stream duration",
                        std::format("{:.6f} s", stream->duration * av_q2d(stream->time_base)));
        }
        if (stream->start_time != AV_NOPTS_VALUE) {
            appendIntegerField(info.decoder_analysis, "Stream start time (ticks)", stream->start_time, true);
            appendField(info.decoder_analysis, "Stream start time",
                        std::format("{:.6f} s", stream->start_time * av_q2d(stream->time_base)));
        }
        appendField(info.decoder_analysis, "Stream time base",
                    std::format("{}/{}", stream->time_base.num, stream->time_base.den));
        appendIntegerField(info.decoder_analysis, "Reported frame count", stream->nb_frames);
        if (par->format >= 0) {
            if (const char* sample_fmt = av_get_sample_fmt_name(static_cast<AVSampleFormat>(par->format)))
                appendField(info.decoder_analysis, "Sample format", sample_fmt);
        }
        const char* profile_name = avcodec_profile_name(par->codec_id, par->profile);
        if (profile_name)
            appendField(info.decoder_analysis, "Profile name", profile_name);

        // Channel layout
        char ch_buf[64]{};
        av_channel_layout_describe(&par->ch_layout, ch_buf, sizeof(ch_buf));
        info.channel_layout = ch_buf;
        appendField(info.decoder_analysis, "Channel layout", info.channel_layout);

        appendDictionaryFields(info.stream_metadata, stream->metadata);

        // Tags from audio stream (Vorbis comment / APEv2 / etc.)
        parseTags(stream->metadata, info);
        parseReplayGain(stream->metadata, fmt_ctx->metadata, info);
    }

    // Tags from container (ID3, etc.), only overwrite if field is empty
    appendDictionaryFields(info.format_metadata, fmt_ctx->metadata);
    parseTags(fmt_ctx->metadata, info);
    if (audio_idx < 0) parseReplayGain(nullptr, fmt_ctx->metadata, info);

    const LyricsContent lyrics =
        selectLyricsContent(readEmbeddedLyrics(audio_idx >= 0 ? fmt_ctx->streams[audio_idx]->metadata : nullptr,
                                              fmt_ctx->metadata),
                            source);
    info.lyrics = lyrics.text;
    info.lyrics_source_kind = lyrics.source_kind;
    info.lyrics_source_path = lyrics.source_path;
    switch (lyrics.source_kind) {
        case LyricsSourceKind::EmbeddedTag:
            appendField(info.decoder_analysis, "Lyrics source", "embedded LYRICS tag");
            break;
        case LyricsSourceKind::SidecarFile:
            appendField(info.decoder_analysis, "Lyrics source",
                        std::format("sidecar file ({})", lyrics.source_path.filename().string()));
            break;
        case LyricsSourceKind::None:
        default:
            break;
    }

    // Album art
    if (options.decode_album_art) {
        const bool has_embedded_album_art = decodeAlbumArt(fmt_ctx, info);
        const auto external_album_art = has_embedded_album_art || source.isUrl()
            ? std::optional<std::filesystem::path>{}
            : decodeExternalAlbumArt(source.path, info);
        if (external_album_art)
            info.external_album_art_path = *external_album_art;
        appendField(info.decoder_analysis, "Album art attached",
                    has_embedded_album_art ? "yes" : "no");
        if (external_album_art)
            appendField(info.decoder_analysis, "Album art external file",
                        external_album_art->filename().string());
        if (!info.album_art_rgba.empty())
            appendField(info.decoder_analysis, "Album art size",
                        std::format("{}x{}", info.album_art_width, info.album_art_height));
    }

    avformat_close_input(&fmt_ctx);
    return info;
}

std::expected<TrackInfo, std::string>
FFmpegMetadataReader::read(const std::filesystem::path& path,
                           MetadataReadOptions options) {
    return read(MediaSource::fromPath(path), options);
}

// ---------------------------------------------------------------------------
// FFmpegMetadataReader::parseTags
// ---------------------------------------------------------------------------

void FFmpegMetadataReader::parseTags(AVDictionary* dict, TrackInfo& info) {
    if (!dict) return;

    auto fill = [&](std::string& field, const char* key) {
        if (!field.empty()) return;  // already filled by a higher-priority source
        std::string v = dictGet(dict, key);
        if (!v.empty()) field = std::move(v);
    };

    fill(info.title,        "title");
    fill(info.artist,       "artist");
    fill(info.album,        "album");
    fill(info.year,         "date");
    fill(info.genre,        "genre");
    fill(info.track_number, "track");
    fill(info.comment,      "comment");
}

// ---------------------------------------------------------------------------
// FFmpegMetadataReader::parseReplayGain
// ---------------------------------------------------------------------------

void FFmpegMetadataReader::parseReplayGain(AVDictionary* stream_dict,
                                           AVDictionary* format_dict,
                                           TrackInfo&    info) {
    // Try stream metadata first, fall back to format metadata.
    auto get = [&](const char* key) -> std::string {
        std::string v = dictGet(stream_dict, key);
        if (v.empty()) v = dictGet(format_dict, key);
        return v;
    };

    if (!info.rg_track_gain_db)
        info.rg_track_gain_db = parseRGFloat(get("REPLAYGAIN_TRACK_GAIN"));
    if (!info.rg_album_gain_db)
        info.rg_album_gain_db = parseRGFloat(get("REPLAYGAIN_ALBUM_GAIN"));
    if (!info.rg_track_peak)
        info.rg_track_peak    = parseRGFloat(get("REPLAYGAIN_TRACK_PEAK"));
    if (!info.rg_album_peak)
        info.rg_album_peak    = parseRGFloat(get("REPLAYGAIN_ALBUM_PEAK"));
}

// ---------------------------------------------------------------------------
// FFmpegMetadataReader::decodeAlbumArt
// ---------------------------------------------------------------------------

bool FFmpegMetadataReader::decodeAlbumArt(AVFormatContext* fmt_ctx, TrackInfo& info) {
    // Find an attached picture stream
    for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i) {
        AVStream* s = fmt_ctx->streams[i];
        if (!(s->disposition & AV_DISPOSITION_ATTACHED_PIC)) continue;

        AVPacket* pkt = &s->attached_pic;
        if (pkt->size <= 0) continue;

        // Decode the image packet
        const AVCodec* codec = avcodec_find_decoder(s->codecpar->codec_id);
        if (!codec) continue;

        AVCodecContext* cc = avcodec_alloc_context3(codec);
        if (!cc) continue;

        if (avcodec_parameters_to_context(cc, s->codecpar) < 0) {
            avcodec_free_context(&cc);
            continue;
        }
        if (avcodec_open2(cc, codec, nullptr) < 0) {
            avcodec_free_context(&cc);
            continue;
        }

        AVFrame* frame = av_frame_alloc();
        if (!frame) {
            avcodec_free_context(&cc);
            continue;
        }
        int ret = avcodec_send_packet(cc, pkt);
        if (ret == 0) ret = avcodec_receive_frame(cc, frame);

        if (ret == 0)
            decodeFrameToAlbumArt(frame, info);

        av_frame_free(&frame);
        avcodec_free_context(&cc);
        return !info.album_art_rgba.empty();
    }
    return false;
}
