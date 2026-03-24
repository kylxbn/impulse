#include "SapMetadataReader.hpp"

#include "core/SupportedFormats.hpp"

extern "C" {
#include <asap-stdio.h>
#include <asap.h>
}

#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <memory>
#include <string_view>

namespace {

using AsapHandle = std::unique_ptr<ASAP, decltype(&ASAP_Delete)>;

std::string safeString(const char* value) {
    return value ? std::string(value) : std::string{};
}

void appendField(std::vector<TrackInfoField>& fields,
                 std::string                   label,
                 std::string                   value) {
    if (value.empty())
        return;

    fields.push_back(TrackInfoField{
        .label = std::move(label),
        .value = std::move(value),
    });
}

void appendIntegerField(std::vector<TrackInfoField>& fields,
                        std::string_view             label,
                        int64_t                      value) {
    if (value <= 0)
        return;

    fields.push_back(TrackInfoField{
        .label = std::string(label),
        .value = std::to_string(value),
    });
}

int64_t estimateBitrateBps(int64_t file_size_bytes,
                           double  duration_seconds) {
    if (file_size_bytes <= 0 || duration_seconds <= 0.0 || !std::isfinite(duration_seconds))
        return 0;

    return static_cast<int64_t>(std::llround(
        static_cast<long double>(file_size_bytes) * 8.0L / duration_seconds));
}

std::string describeChannelMode(int channels) {
    return channels >= 2 ? "stereo dual POKEY" : "mono POKEY";
}

std::expected<TrackInfo, std::string>
readSapMetadataImpl(const MediaSource& source,
                    MetadataReadOptions /*options*/) {
    if (!source.isFile())
        return std::unexpected(std::format("ASAP only supports local files: {}", source.string()));

    if (!isSapExtension(source.extension()))
        return std::unexpected(std::format("Unsupported SAP extension: {}", source.string()));

    AsapHandle asap(ASAP_New(), &ASAP_Delete);
    if (!asap)
        return std::unexpected("Cannot create ASAP decoder instance");

    ASAP_SetSampleRate(asap.get(), ASAP_SAMPLE_RATE);

    const std::string path_string = source.path.string();
    if (!ASAP_LoadFiles(asap.get(), path_string.c_str(), ASAPFileLoader_GetStdio()))
        return std::unexpected(std::format("Cannot load SAP file: {}", source.string()));

    const ASAPInfo* raw_info = ASAP_GetInfo(asap.get());
    if (!raw_info)
        return std::unexpected(std::format("Cannot inspect SAP metadata: {}", source.string()));

    const int songs = std::max(ASAPInfo_GetSongs(raw_info), 1);
    int selected_song = ASAPInfo_GetDefaultSong(raw_info);
    if (selected_song < 0 || selected_song >= songs)
        selected_song = 0;

    const int duration_ms = ASAPInfo_GetDuration(raw_info, selected_song);
    const int channels = std::clamp(ASAPInfo_GetChannels(raw_info), 1, 2);
    const int type_letter = ASAPInfo_GetTypeLetter(raw_info);
    const bool loops = ASAPInfo_GetLoop(raw_info, selected_song);
    const std::string original_ext = safeString(ASAPInfo_GetOriginalModuleExt(raw_info));

    TrackInfo info;
    info.source = source;
    info.decoder_name = "ASAP";
    info.path = source.path;
    info.is_stream = false;
    info.seekable = true;
    info.finite_duration = duration_ms >= 0;
    info.duration_seconds = info.finite_duration
        ? static_cast<double>(duration_ms) / 1000.0
        : 0.0;
    info.codec_name = "SAP";
    info.container_format = "sap";
    info.sample_rate = ASAP_SAMPLE_RATE;
    info.bit_depth = 16;
    info.channels = channels;
    info.channel_layout = channels >= 2 ? "stereo" : "mono";
    info.track_number = std::to_string(selected_song + 1);
    info.title = safeString(ASAPInfo_GetTitle(raw_info));
    info.artist = safeString(ASAPInfo_GetAuthor(raw_info));
    info.year = safeString(ASAPInfo_GetDate(raw_info));

    std::error_code file_size_ec;
    info.file_size_bytes = static_cast<int64_t>(std::filesystem::file_size(source.path, file_size_ec));
    if (file_size_ec)
        info.file_size_bytes = 0;
    info.bitrate_bps = estimateBitrateBps(info.file_size_bytes, info.duration_seconds);

    appendField(info.format_metadata, "Author", info.artist);
    appendField(info.format_metadata, "Title", info.title);
    appendField(info.format_metadata, "Date", info.year);
    appendIntegerField(info.format_metadata, "Subsongs", songs);
    appendIntegerField(info.format_metadata, "Default song", selected_song + 1);
    appendField(info.format_metadata, "Original module extension", original_ext);
    if (!original_ext.empty()) {
        appendField(info.format_metadata,
                    "Original module format",
                    safeString(ASAPInfo_GetExtDescription(original_ext.c_str())));
    }

    info.decoder_analysis.push_back(TrackInfoField{
        .label = "Source",
        .value = source.string(),
    });
    appendIntegerField(info.decoder_analysis, "File size (bytes)", info.file_size_bytes);
    appendIntegerField(info.decoder_analysis, "Estimated bitrate (bps)", info.bitrate_bps);
    if (info.duration_seconds > 0.0) {
        info.decoder_analysis.push_back(TrackInfoField{
            .label = "Track duration",
            .value = std::format("{:.3f} s", info.duration_seconds),
        });
    }
    info.decoder_analysis.push_back(TrackInfoField{
        .label = "TV system",
        .value = ASAPInfo_IsNtsc(raw_info) ? "NTSC" : "PAL",
    });
    info.decoder_analysis.push_back(TrackInfoField{
        .label = "Channel mode",
        .value = describeChannelMode(channels),
    });
    if (type_letter > 0) {
        info.decoder_analysis.push_back(TrackInfoField{
            .label = "SAP type",
            .value = std::string(1, static_cast<char>(type_letter)),
        });
    }
    appendIntegerField(info.decoder_analysis,
                       "Player rate (scanlines)",
                       ASAPInfo_GetPlayerRateScanlines(raw_info));
    appendIntegerField(info.decoder_analysis,
                       "Player rate (Hz)",
                       ASAPInfo_GetPlayerRateHz(raw_info));
    appendField(info.decoder_analysis, "Original module extension", original_ext);
    info.decoder_analysis.push_back(TrackInfoField{
        .label = "Selected song loops",
        .value = loops ? "yes" : "no",
    });
    info.decoder_analysis.push_back(TrackInfoField{
        .label = "Seek support",
        .value = "enabled via ASAP millisecond seek",
    });
    info.decoder_analysis.push_back(TrackInfoField{
        .label = "Gapless preload",
        .value = "not supported",
    });
    info.decoder_analysis.push_back(TrackInfoField{
        .label = "Render sample rate",
        .value = std::format("{} Hz", ASAP_SAMPLE_RATE),
    });
    info.decoder_analysis.push_back(TrackInfoField{
        .label = "ASAP version",
        .value = ASAPInfo_VERSION,
    });

    return info;
}

}  // namespace

std::expected<TrackInfo, std::string>
SapMetadataReader::read(const MediaSource& source,
                        MetadataReadOptions options) {
    try {
        return readSapMetadataImpl(source, options);
    } catch (const std::exception& ex) {
        return std::unexpected(ex.what());
    }
}

std::expected<TrackInfo, std::string>
SapMetadataReader::read(const std::filesystem::path& path,
                        MetadataReadOptions options) {
    return read(MediaSource::fromPath(path), options);
}
