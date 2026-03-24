#include "SapDecoderBackend.hpp"

#include "audio/ReplayGain.hpp"
#include "core/SupportedFormats.hpp"
#include "metadata/SapMetadataReader.hpp"

extern "C" {
#include <asap-stdio.h>
#include <asap.h>
}

#include <algorithm>
#include <cmath>
#include <format>
#include <ranges>
#include <span>
#include <string_view>

namespace {

constexpr float kInt16ToFloatScale = 1.0f / 32768.0f;

void upsertAnalysisField(std::vector<TrackInfoField>& fields,
                         std::string_view            label,
                         std::string                 value) {
    const auto it = std::ranges::find(fields, label, &TrackInfoField::label);
    if (it != fields.end()) {
        it->value = std::move(value);
        return;
    }

    fields.push_back(TrackInfoField{
        .label = std::string(label),
        .value = std::move(value),
    });
}

class SapDecoderProvider final : public DecoderProvider {
public:
    [[nodiscard]] std::string_view name() const override {
        return "ASAP";
    }

    [[nodiscard]] bool supportsSource(const MediaSource& source) const override {
        return source.isFile() && isSapExtension(source.extension());
    }

    [[nodiscard]] DecoderCapabilities capabilitiesForSource(const MediaSource& /*source*/) const override {
        return DecoderCapabilities{
            .can_seek = true,
            .supports_gapless = false,
        };
    }

    [[nodiscard]] std::unique_ptr<DecoderBackend> createBackend() const override {
        return std::make_unique<SapDecoderBackend>();
    }

    std::expected<TrackInfo, std::string>
    readMetadata(const MediaSource& source,
                 MetadataReadOptions options) const override {
        return SapMetadataReader::read(source, options);
    }
};

}  // namespace

SapDecoderBackend::SapDecoderBackend() = default;

SapDecoderBackend::~SapDecoderBackend() {
    close();
}

DecoderBackend::OpenResult SapDecoderBackend::open(const MediaSource& source,
                                                   int output_sample_rate) {
    close();

    if (!source.isFile())
        return {false, "ASAP only supports local files"};

    if (!isSapExtension(source.extension()))
        return {false, std::format("Unsupported SAP file: {}", source.string())};

    asap_ = ASAP_New();
    if (!asap_)
        return {false, "Cannot create ASAP decoder instance"};

    const int sample_rate = std::max(output_sample_rate, 1);
    ASAP_SetSampleRate(asap_, sample_rate);

    const std::string path_string = source.path.string();
    if (!ASAP_LoadFiles(asap_, path_string.c_str(), ASAPFileLoader_GetStdio())) {
        close();
        return {false, "Cannot load SAP file"};
    }

    const ASAPInfo* raw_info = ASAP_GetInfo(asap_);
    if (!raw_info) {
        close();
        return {false, "Cannot inspect SAP metadata"};
    }

    const int songs = std::max(ASAPInfo_GetSongs(raw_info), 1);
    int selected_song = ASAPInfo_GetDefaultSong(raw_info);
    if (selected_song < 0 || selected_song >= songs)
        selected_song = 0;

    const int duration_ms = ASAPInfo_GetDuration(raw_info, selected_song);
    if (!ASAP_PlaySong(asap_, selected_song, duration_ms)) {
        close();
        return {false, "Cannot start SAP playback"};
    }

    auto metadata = SapMetadataReader::read(source, MetadataReadOptions{.decode_album_art = false});
    if (!metadata) {
        close();
        return {false, metadata.error()};
    }

    track_info_ = std::move(*metadata);
    source_channels_ = std::clamp(track_info_.channels, 1, 2);
    out_fmt_.sample_rate = sample_rate;
    out_fmt_.channels = 2;
    track_info_.sample_rate = sample_rate;
    upsertAnalysisField(track_info_.decoder_analysis,
                        "Render sample rate",
                        std::format("{} Hz", sample_rate));
    upsertAnalysisField(track_info_.decoder_analysis,
                        "Output mix",
                        source_channels_ >= 2
                            ? "native stereo"
                            : "mono duplicated to stereo");

    total_frames_ = 0;
    if (track_info_.finite_duration && track_info_.duration_seconds > 0.0) {
        total_frames_ = static_cast<int64_t>(std::llround(
            track_info_.duration_seconds * static_cast<double>(sample_rate)));
    }

    estimated_bitrate_bps_ = track_info_.bitrate_bps;
    replay_gain_scale_ = ReplayGain::linearGainForTrack(track_info_, replay_gain_settings_);
    render_buffer_.assign(kRenderChunkFrames * static_cast<size_t>(source_channels_) * sizeof(int16_t), 0);

    return {true, {}};
}

void SapDecoderBackend::close() {
    if (asap_) {
        ASAP_Delete(asap_);
        asap_ = nullptr;
    }

    out_fmt_ = {};
    track_info_ = {};
    source_channels_ = 1;
    total_frames_ = 0;
    estimated_bitrate_bps_ = 0;
    replay_gain_scale_ = 1.0f;
    render_buffer_.clear();
}

bool SapDecoderBackend::isOpen() const {
    return asap_ != nullptr;
}

int SapDecoderBackend::decodeNextFrames(std::vector<float>& out_pcm) {
    out_pcm.clear();
    if (!asap_)
        return -1;

    const size_t bytes_per_frame =
        static_cast<size_t>(std::max(source_channels_, 1)) * sizeof(int16_t);
    const size_t requested_bytes = kRenderChunkFrames * bytes_per_frame;
    if (render_buffer_.size() < requested_bytes)
        render_buffer_.assign(requested_bytes, 0);

    const int generated = ASAP_Generate(asap_,
                                        render_buffer_.data(),
                                        static_cast<int>(requested_bytes),
                                        ASAPSampleFormat_S16_L_E);
    if (generated < 0)
        return -1;
    if (generated == 0)
        return 0;

    const int frames = generated / static_cast<int>(bytes_per_frame);
    if (frames <= 0)
        return -1;

    const auto* samples = reinterpret_cast<const int16_t*>(render_buffer_.data());
    out_pcm.resize(static_cast<size_t>(frames) * 2);

    if (source_channels_ >= 2) {
        for (int index = 0; index < frames; ++index) {
            out_pcm[static_cast<size_t>(index) * 2] =
                static_cast<float>(samples[static_cast<size_t>(index) * 2]) * kInt16ToFloatScale;
            out_pcm[static_cast<size_t>(index) * 2 + 1] =
                static_cast<float>(samples[static_cast<size_t>(index) * 2 + 1]) * kInt16ToFloatScale;
        }
    } else {
        for (int index = 0; index < frames; ++index) {
            const float sample =
                static_cast<float>(samples[static_cast<size_t>(index)]) * kInt16ToFloatScale;
            out_pcm[static_cast<size_t>(index) * 2] = sample;
            out_pcm[static_cast<size_t>(index) * 2 + 1] = sample;
        }
    }

    ReplayGain::apply(std::span<float>(out_pcm), replay_gain_scale_);
    return frames;
}

std::optional<TrackInfo> SapDecoderBackend::consumeTrackInfoUpdate() {
    return std::nullopt;
}

bool SapDecoderBackend::seek(double position_seconds) {
    if (!asap_ || !track_info_.seekable)
        return false;

    const double clamped_seconds = std::clamp(position_seconds,
                                              0.0,
                                              track_info_.duration_seconds > 0.0
                                                  ? track_info_.duration_seconds
                                                  : position_seconds);
    const int position_ms = static_cast<int>(std::llround(clamped_seconds * 1000.0));
    return ASAP_Seek(asap_, position_ms);
}

void SapDecoderBackend::setReplayGainSettings(ReplayGain::ReplayGainSettings settings) {
    replay_gain_settings_ = settings;
    replay_gain_scale_ = ReplayGain::linearGainForTrack(track_info_, settings);
}

DecoderCapabilities SapDecoderBackend::capabilities() const {
    if (!isOpen())
        return {};

    return DecoderCapabilities{
        .can_seek = true,
        .supports_gapless = false,
    };
}

const DecoderProvider& sapDecoderProvider() {
    static const SapDecoderProvider kProvider;
    return kProvider;
}
