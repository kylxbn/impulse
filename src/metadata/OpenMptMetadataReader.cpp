#include "OpenMptMetadataReader.hpp"

#include <libopenmpt/libopenmpt.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace {

constexpr int kOpenMptRenderSampleRate = 48000;

std::string firstNonEmpty(std::initializer_list<std::string_view> values) {
    for (const std::string_view value : values) {
        if (!value.empty())
            return std::string(value);
    }
    return {};
}

void appendField(std::vector<TrackInfoField>& fields,
                 std::string_view label,
                 std::string_view value) {
    if (value.empty())
        return;

    fields.push_back(TrackInfoField{
        .label = std::string(label),
        .value = std::string(value),
    });
}

void appendIntegerField(std::vector<TrackInfoField>& fields,
                        std::string_view label,
                        int64_t value,
                        bool allow_zero = false) {
    if (!allow_zero && value == 0)
        return;

    fields.push_back(TrackInfoField{
        .label = std::string(label),
        .value = std::to_string(value),
    });
}

int64_t estimateBitrateBps(int64_t file_size_bytes,
                           double duration_seconds) {
    if (file_size_bytes <= 0 || duration_seconds <= 0.0 || !std::isfinite(duration_seconds))
        return 0;

    return static_cast<int64_t>(std::llround(
        static_cast<long double>(file_size_bytes) * 8.0L / duration_seconds));
}

std::expected<TrackInfo, std::string>
readOpenMptMetadataImpl(const MediaSource& source,
                        MetadataReadOptions /*options*/) {
    if (!source.isFile())
        return std::unexpected(std::format("libopenmpt only supports local files: {}", source.string()));

    std::ifstream stream(source.path, std::ios::binary);
    if (!stream)
        return std::unexpected(std::format("Cannot open tracker module: {}", source.string()));

    openmpt::module module = [&stream]() {
        return openmpt::module(stream);
    }();

    module.set_repeat_count(0);

    const std::string type = module.get_metadata("type");
    const std::string type_long = module.get_metadata("type_long");
    const std::string original_type = module.get_metadata("originaltype");
    const std::string original_type_long = module.get_metadata("originaltype_long");
    const std::string container = module.get_metadata("container");
    const std::string container_long = module.get_metadata("container_long");
    const std::string tracker = module.get_metadata("tracker");
    const std::string warnings = module.get_metadata("warnings");
    const std::string title = module.get_metadata("title");
    const std::string artist = module.get_metadata("artist");
    const std::string date = module.get_metadata("date");
    const std::string message_raw = module.get_metadata("message_raw");
    const std::string message_fallback = module.get_metadata("message");
    const std::string message = firstNonEmpty({
        message_raw,
        message_fallback,
    });

    TrackInfo info;
    info.source = source;
    info.decoder_name = "libopenmpt";
    info.path = source.path;
    info.is_stream = false;
    info.seekable = true;
    info.sample_rate = kOpenMptRenderSampleRate;
    info.channels = 2;
    info.channel_layout = "stereo";
    info.title = title;
    info.artist = artist;
    info.year = date;
    info.comment = message;

    std::error_code file_size_ec;
    info.file_size_bytes = static_cast<int64_t>(std::filesystem::file_size(source.path, file_size_ec));
    if (file_size_ec)
        info.file_size_bytes = 0;

    info.duration_seconds = module.get_duration_seconds();
    info.finite_duration = info.duration_seconds > 0.0 && std::isfinite(info.duration_seconds);
    if (!info.finite_duration)
        info.duration_seconds = 0.0;

    info.bitrate_bps = estimateBitrateBps(info.file_size_bytes, info.duration_seconds);
    info.codec_name = firstNonEmpty({
        original_type_long,
        type_long,
        original_type,
        type,
    });
    info.container_format = firstNonEmpty({
        container,
        original_type,
        type,
        source.extension(),
    });
    if (!info.container_format.empty() && info.container_format.front() == '.')
        info.container_format.erase(info.container_format.begin());

    appendField(info.format_metadata, "Format", type);
    appendField(info.format_metadata, "Format (Long)", type_long);
    appendField(info.format_metadata, "Original Format", original_type);
    appendField(info.format_metadata, "Original Format (Long)", original_type_long);
    appendField(info.format_metadata, "Container", container);
    appendField(info.format_metadata, "Container (Long)", container_long);
    appendField(info.format_metadata, "Tracker", tracker);
    appendField(info.format_metadata, "Warnings", warnings);
    appendField(info.format_metadata, "Message", message);

    info.decoder_analysis.push_back(TrackInfoField{
        .label = "Source",
        .value = source.string(),
    });
    appendIntegerField(info.decoder_analysis, "File size (bytes)", info.file_size_bytes);
    if (info.duration_seconds > 0.0) {
        info.decoder_analysis.push_back(TrackInfoField{
            .label = "Track duration",
            .value = std::format("{:.6f} s", info.duration_seconds),
        });
    }
    appendIntegerField(info.decoder_analysis, "Estimated bitrate (bps)", info.bitrate_bps);
    info.decoder_analysis.push_back(TrackInfoField{
        .label = "Seek support",
        .value = "enabled via libopenmpt time seek",
    });
    info.decoder_analysis.push_back(TrackInfoField{
        .label = "Gapless preload",
        .value = "supported",
    });
    info.decoder_analysis.push_back(TrackInfoField{
        .label = "Render sample rate",
        .value = std::format("{} Hz", kOpenMptRenderSampleRate),
    });
    info.decoder_analysis.push_back(TrackInfoField{
        .label = "Interpolation",
        .value = "disabled (1-tap / no interpolation)",
    });
    appendIntegerField(info.decoder_analysis, "Module channels", module.get_num_channels());
    appendIntegerField(info.decoder_analysis, "Orders", module.get_num_orders());
    appendIntegerField(info.decoder_analysis, "Patterns", module.get_num_patterns());
    appendIntegerField(info.decoder_analysis, "Samples", module.get_num_samples(), true);
    appendIntegerField(info.decoder_analysis, "Instruments", module.get_num_instruments(), true);
    appendIntegerField(info.decoder_analysis, "Subsongs", module.get_num_subsongs(), true);

    const double bpm = module.get_current_estimated_bpm();
    if (bpm > 0.0 && std::isfinite(bpm)) {
        info.decoder_analysis.push_back(TrackInfoField{
            .label = "Estimated BPM",
            .value = std::format("{:.2f}", bpm),
        });
    }

    return info;
}

}  // namespace

std::expected<TrackInfo, std::string>
OpenMptMetadataReader::read(const MediaSource& source,
                            MetadataReadOptions options) {
    try {
        return readOpenMptMetadataImpl(source, options);
    } catch (const std::exception& ex) {
        return std::unexpected(ex.what());
    }
}

std::expected<TrackInfo, std::string>
OpenMptMetadataReader::read(const std::filesystem::path& path,
                            MetadataReadOptions options) {
    return read(MediaSource::fromPath(path), options);
}
