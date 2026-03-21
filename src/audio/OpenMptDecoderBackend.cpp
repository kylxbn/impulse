#include "OpenMptDecoderBackend.hpp"

#include "audio/ReplayGain.hpp"
#include "core/SupportedFormats.hpp"
#include "metadata/OpenMptMetadataReader.hpp"

#include <libopenmpt/libopenmpt.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ranges>
#include <span>
#include <vector>

namespace {

constexpr int kOpenMptRenderSampleRate = 48000;
constexpr int kOpenMptNoInterpolationFilterLength = 1;

bool probeOpenMptFile(const std::filesystem::path& path) {
    std::error_code file_size_ec;
    const uint64_t file_size = std::filesystem::file_size(path, file_size_ec);
    if (file_size_ec || file_size == 0)
        return false;

    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return false;

    const auto recommended_size = openmpt::probe_file_header_get_recommended_size();
    const auto bytes_to_read = static_cast<size_t>(std::min<uint64_t>(file_size, recommended_size));
    std::vector<std::uint8_t> header(bytes_to_read);
    stream.read(reinterpret_cast<char*>(header.data()),
                static_cast<std::streamsize>(header.size()));
    header.resize(static_cast<size_t>(stream.gcount()));
    if (header.empty())
        return false;

    const int result = openmpt::probe_file_header(openmpt::probe_file_header_flags_default2,
                                                  header.data(),
                                                  header.size(),
                                                  file_size);
    return result == openmpt::probe_file_header_result_success;
}

void upsertAnalysisField(std::vector<TrackInfoField>& fields,
                         std::string_view label,
                         std::string value) {
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

class OpenMptDecoderProvider final : public DecoderProvider {
public:
    [[nodiscard]] std::string_view name() const override {
        return "libopenmpt";
    }

    [[nodiscard]] bool supportsSource(const MediaSource& source) const override {
        if (!source.isFile())
            return false;

        if (isTrackerModuleExtension(source.extension()))
            return true;

        return probeOpenMptFile(source.path);
    }

    [[nodiscard]] DecoderCapabilities capabilitiesForSource(const MediaSource& /*source*/) const override {
        return DecoderCapabilities{
            .can_seek = true,
            .supports_gapless = true,
        };
    }

    [[nodiscard]] std::unique_ptr<DecoderBackend> createBackend() const override {
        return std::make_unique<OpenMptDecoderBackend>();
    }

    std::expected<TrackInfo, std::string>
    readMetadata(const MediaSource& source,
                 MetadataReadOptions options) const override {
        return OpenMptMetadataReader::read(source, options);
    }
};

}  // namespace

OpenMptDecoderBackend::OpenMptDecoderBackend() = default;

OpenMptDecoderBackend::~OpenMptDecoderBackend() {
    close();
}

DecoderBackend::OpenResult OpenMptDecoderBackend::open(const MediaSource& source,
                                                       int output_sample_rate) {
    close();

    if (!source.isFile())
        return {false, "libopenmpt only supports local tracker files"};

    if (output_sample_rate != kOpenMptRenderSampleRate) {
        return {false,
                std::format("libopenmpt backend expects {} Hz render rate, got {} Hz",
                            kOpenMptRenderSampleRate,
                            output_sample_rate)};
    }

    std::ifstream stream(source.path, std::ios::binary);
    if (!stream)
        return {false, std::format("Cannot open tracker module: {}", source.string())};

    try {
        module_ = std::make_unique<openmpt::module>(stream);
        module_->set_repeat_count(0);
        module_->set_render_param(openmpt::module::RENDER_INTERPOLATIONFILTER_LENGTH,
                                  kOpenMptNoInterpolationFilterLength);
    } catch (const std::exception& ex) {
        close();
        return {false, std::format("Cannot open tracker module: {}", ex.what())};
    }

    auto metadata = OpenMptMetadataReader::read(source, MetadataReadOptions{.decode_album_art = false});
    if (!metadata) {
        close();
        return {false, metadata.error()};
    }

    track_info_ = std::move(*metadata);
    out_fmt_.sample_rate = output_sample_rate;
    out_fmt_.channels = 2;
    track_info_.sample_rate = output_sample_rate;
    upsertAnalysisField(track_info_.decoder_analysis,
                        "Render sample rate",
                        std::format("{} Hz", output_sample_rate));
    upsertAnalysisField(track_info_.decoder_analysis,
                        "Interpolation",
                        "disabled (1-tap / no interpolation)");

    total_frames_ = 0;
    if (track_info_.finite_duration && track_info_.duration_seconds > 0.0) {
        total_frames_ = static_cast<int64_t>(std::llround(
            track_info_.duration_seconds * static_cast<double>(output_sample_rate)));
    }

    estimated_bitrate_bps_ = track_info_.bitrate_bps;
    replay_gain_scale_ = ReplayGain::linearGainForTrack(track_info_, replay_gain_settings_);

    return {true, {}};
}

void OpenMptDecoderBackend::close() {
    module_.reset();
    out_fmt_ = {};
    track_info_ = {};
    total_frames_ = 0;
    estimated_bitrate_bps_ = 0;
    replay_gain_scale_ = 1.0f;
}

bool OpenMptDecoderBackend::isOpen() const {
    return module_ != nullptr;
}

int OpenMptDecoderBackend::decodeNextFrames(std::vector<float>& out_pcm) {
    out_pcm.clear();
    if (!module_ || out_fmt_.sample_rate <= 0)
        return -1;

    out_pcm.assign(kRenderChunkFrames * 2, 0.0f);

    size_t frames = 0;
    try {
        frames = module_->read_interleaved_stereo(out_fmt_.sample_rate,
                                                  kRenderChunkFrames,
                                                  out_pcm.data());
    } catch (const std::exception&) {
        out_pcm.clear();
        return -1;
    }

    if (frames == 0) {
        out_pcm.clear();
        return 0;
    }

    out_pcm.resize(frames * 2);
    ReplayGain::apply(std::span<float>(out_pcm), replay_gain_scale_);
    return static_cast<int>(frames);
}

std::optional<TrackInfo> OpenMptDecoderBackend::consumeTrackInfoUpdate() {
    return std::nullopt;
}

bool OpenMptDecoderBackend::seek(double position_seconds) {
    if (!module_)
        return false;

    const double max_seconds = track_info_.finite_duration && track_info_.duration_seconds > 0.0
        ? track_info_.duration_seconds
        : position_seconds;
    const double clamped = std::clamp(position_seconds, 0.0, max_seconds);

    try {
        const double result = module_->set_position_seconds(clamped);
        return std::isfinite(result) && result >= 0.0;
    } catch (const std::exception&) {
        return false;
    }
}

void OpenMptDecoderBackend::setReplayGainSettings(ReplayGain::ReplayGainSettings settings) {
    replay_gain_settings_ = settings;
    replay_gain_scale_ = ReplayGain::linearGainForTrack(track_info_, settings);
}

const DecoderProvider& openMptDecoderProvider() {
    static const OpenMptDecoderProvider kProvider;
    return kProvider;
}
