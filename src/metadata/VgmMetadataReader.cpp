#include "VgmMetadataReader.hpp"

#include "emu/Resampler.h"
#include "player/playerbase.hpp"
#include "player/vgmplayer.hpp"
#include "utils/DataLoader.h"
#include "utils/FileLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <map>
#include <string_view>
#include <vector>

namespace {

constexpr UINT32 kNoLoopTick = std::numeric_limits<UINT32>::max();

struct ScopedVgmLoader {
    explicit ScopedVgmLoader(const std::filesystem::path& path)
        : loader(FileLoader_Init(path.c_str())) {
    }

    ~ScopedVgmLoader() {
        if (loader)
            DataLoader_Deinit(loader);
    }

    ScopedVgmLoader(const ScopedVgmLoader&) = delete;
    ScopedVgmLoader& operator=(const ScopedVgmLoader&) = delete;

    [[nodiscard]] bool valid() const {
        return loader != nullptr;
    }

    DATA_LOADER* loader = nullptr;
};

struct VgmGd3Tags {
    std::string title_en;
    std::string title_jp;
    std::string game_en;
    std::string game_jp;
    std::string system_en;
    std::string system_jp;
    std::string artist_en;
    std::string artist_jp;
    std::string date;
    std::string encoded_by;
    std::string comment;
};

std::string firstNonEmpty(std::initializer_list<std::string_view> values) {
    for (const std::string_view value : values) {
        if (!value.empty())
            return std::string(value);
    }
    return {};
}

void appendMetadataField(std::vector<TrackInfoField>& fields,
                         std::string_view label,
                         std::string_view value) {
    if (value.empty())
        return;

    fields.push_back(TrackInfoField{
        .label = std::string(label),
        .value = std::string(value),
    });
}

void appendIntegerAnalysisField(std::vector<TrackInfoField>& fields,
                                std::string_view label,
                                int64_t value) {
    if (value <= 0)
        return;

    fields.push_back(TrackInfoField{
        .label = std::string(label),
        .value = std::to_string(value),
    });
}

int sampleRateForSongInfo(const PLR_SONG_INFO& song_info) {
    if (song_info.tickRateMul == 0 || song_info.tickRateDiv == 0)
        return 0;

    const double samples_per_second =
        static_cast<double>(song_info.tickRateDiv) /
        static_cast<double>(song_info.tickRateMul);
    return samples_per_second > 0.0
        ? static_cast<int>(std::llround(samples_per_second))
        : 0;
}

int64_t estimateBitrateBps(int64_t file_size_bytes,
                           double duration_seconds) {
    if (file_size_bytes <= 0 || duration_seconds <= 0.0)
        return 0;

    return static_cast<int64_t>(std::llround(
        static_cast<long double>(file_size_bytes) * 8.0L / duration_seconds));
}

VgmGd3Tags readGd3Tags(VGMPlayer& player) {
    VgmGd3Tags tags;

    const char* const* raw_tags = player.GetTags();
    if (!raw_tags)
        return tags;

    for (size_t index = 0; raw_tags[index] && raw_tags[index + 1]; index += 2) {
        const std::string_view key = raw_tags[index];
        const std::string_view value = raw_tags[index + 1];

        if (key == "TITLE")
            tags.title_en = value;
        else if (key == "TITLE-JPN")
            tags.title_jp = value;
        else if (key == "GAME")
            tags.game_en = value;
        else if (key == "GAME-JPN")
            tags.game_jp = value;
        else if (key == "SYSTEM")
            tags.system_en = value;
        else if (key == "SYSTEM-JPN")
            tags.system_jp = value;
        else if (key == "ARTIST")
            tags.artist_en = value;
        else if (key == "ARTIST-JPN")
            tags.artist_jp = value;
        else if (key == "DATE")
            tags.date = value;
        else if (key == "ENCODED_BY")
            tags.encoded_by = value;
        else if (key == "COMMENT")
            tags.comment = value;
    }

    return tags;
}

void applyGd3Tags(const VgmGd3Tags& tags, TrackInfo& info) {
    info.title = firstNonEmpty({tags.title_en, tags.title_jp});
    info.album = firstNonEmpty({tags.game_en, tags.game_jp});
    info.genre = firstNonEmpty({tags.system_en, tags.system_jp});
    info.artist = firstNonEmpty({tags.artist_en, tags.artist_jp});
    info.year = tags.date;
    info.comment = tags.comment;

    appendMetadataField(info.format_metadata, "Title", tags.title_en);
    appendMetadataField(info.format_metadata, "Title (Japanese)", tags.title_jp);
    appendMetadataField(info.format_metadata, "Game", tags.game_en);
    appendMetadataField(info.format_metadata, "Game (Japanese)", tags.game_jp);
    appendMetadataField(info.format_metadata, "System", tags.system_en);
    appendMetadataField(info.format_metadata, "System (Japanese)", tags.system_jp);
    appendMetadataField(info.format_metadata, "Artist", tags.artist_en);
    appendMetadataField(info.format_metadata, "Artist (Japanese)", tags.artist_jp);
    appendMetadataField(info.format_metadata, "Date", tags.date);
    appendMetadataField(info.format_metadata, "Encoded By", tags.encoded_by);
    appendMetadataField(info.format_metadata, "Comment", tags.comment);
}

std::string describeDevice(const PLR_DEV_INFO& device) {
    std::string name = "Unknown chip";
    if (device.devDecl && device.devDecl->name) {
        if (const char* declared_name = device.devDecl->name(device.devCfg);
            declared_name && declared_name[0] != '\0') {
            name = declared_name;
        }
    }

    if (device.instance > 0)
        name += std::format(" #{}", static_cast<uint32_t>(device.instance) + 1u);

    if (device.devCfg && device.devCfg->clock > 0)
        name += std::format(" @ {} Hz", device.devCfg->clock);

    return name;
}

void appendDeviceAnalysis(VGMPlayer& player, TrackInfo& info) {
    std::vector<PLR_DEV_INFO> devices;
    if (player.GetSongDeviceInfo(devices) == 0xFF)
        return;

    std::map<std::string, size_t> device_counts;
    std::vector<std::string> primary_devices;
    for (const PLR_DEV_INFO& device : devices) {
        if (device.parentIdx != static_cast<UINT32>(-1))
            continue;

        std::string name = "Unknown chip";
        if (device.devDecl && device.devDecl->name) {
            if (const char* declared_name = device.devDecl->name(device.devCfg);
                declared_name && declared_name[0] != '\0') {
                name = declared_name;
            }
        }

        device_counts[name] += 1;
        primary_devices.push_back(describeDevice(device));
    }

    if (!device_counts.empty()) {
        std::string summary;
        bool first = true;
        for (const auto& [name, count] : device_counts) {
            if (!first)
                summary += ", ";
            if (count > 1)
                summary += std::format("{}x {}", count, name);
            else
                summary += name;
            first = false;
        }

        info.decoder_analysis.push_back(TrackInfoField{
            .label = "Chip summary",
            .value = std::move(summary),
        });
    }

    for (size_t index = 0; index < primary_devices.size(); ++index) {
        info.decoder_analysis.push_back(TrackInfoField{
            .label = std::format("Chip {}", index + 1),
            .value = primary_devices[index],
        });
    }
}

std::expected<TrackInfo, std::string>
readVgmMetadataImpl(const MediaSource& source,
                    MetadataReadOptions /*options*/) {
    ScopedVgmLoader loader(source.path);
    if (!loader.valid())
        return std::unexpected(std::format("Cannot open file: {}", source.string()));

    DataLoader_SetPreloadBytes(loader.loader, 0x100);
    if (DataLoader_Load(loader.loader) != 0) {
        return std::unexpected(std::format("Cannot read VGM data: {}", source.string()));
    }

    VGMPlayer player;
    if (player.SetSampleRate(44100) != 0 || player.LoadFile(loader.loader) != 0) {
        return std::unexpected(std::format("Cannot parse VGM file: {}", source.string()));
    }

    const VGM_HEADER* header = player.GetFileHeader();
    if (!header) {
        return std::unexpected(std::format("Cannot inspect VGM header: {}", source.string()));
    }

    PLR_SONG_INFO song_info{};
    if (player.GetSongInfo(song_info) == 0xFF) {
        player.UnloadFile();
        return std::unexpected(std::format("Cannot inspect VGM song info: {}", source.string()));
    }

    const VgmGd3Tags tags = readGd3Tags(player);

    TrackInfo info;
    info.source = source;
    info.decoder_name = "libvgm";
    info.path = source.path;
    info.seekable = true;
    info.is_stream = false;
    info.codec_name = "VGM";
    info.container_format = source.extension().empty()
        ? "vgm"
        : source.extension().substr(1);
    info.sample_rate = sampleRateForSongInfo(song_info);
    info.channels = 2;
    info.channel_layout = "stereo";
    info.duration_seconds = player.Tick2Second(song_info.songLen);
    info.finite_duration = info.duration_seconds > 0.0;

    std::error_code file_size_ec;
    info.file_size_bytes = static_cast<int64_t>(std::filesystem::file_size(source.path, file_size_ec));
    if (file_size_ec)
        info.file_size_bytes = 0;
    info.bitrate_bps = estimateBitrateBps(info.file_size_bytes, info.duration_seconds);

    applyGd3Tags(tags, info);

    info.decoder_analysis.push_back(TrackInfoField{
        .label = "Source",
        .value = source.string(),
    });
    appendIntegerAnalysisField(info.decoder_analysis, "File size (bytes)", info.file_size_bytes);
    if (info.duration_seconds > 0.0) {
        info.decoder_analysis.push_back(TrackInfoField{
            .label = "Track duration",
            .value = std::format("{:.6f} s", info.duration_seconds),
        });
    }
    appendIntegerAnalysisField(info.decoder_analysis, "Estimated bitrate (bps)", info.bitrate_bps);
    if (info.sample_rate > 0) {
        info.decoder_analysis.push_back(TrackInfoField{
            .label = "Timing sample rate",
            .value = std::format("{} Hz", info.sample_rate),
        });
    }
    info.decoder_analysis.push_back(TrackInfoField{
        .label = "Looping",
        .value = header->loopOfs != 0 ? "yes" : "no",
    });
    if (song_info.loopTick != kNoLoopTick && song_info.loopTick > 0) {
        info.decoder_analysis.push_back(TrackInfoField{
            .label = "Loop length",
            .value = std::format("{:.6f} s", player.Tick2Second(player.GetLoopTicks())),
        });
    }
    info.decoder_analysis.push_back(TrackInfoField{
        .label = "Playback stop behavior",
        .value = header->loopOfs != 0
            ? "stop at first loop boundary"
            : "stop at end of stream",
    });
    info.decoder_analysis.push_back(TrackInfoField{
        .label = "Seek support",
        .value = "enabled via libvgm sample seek",
    });
    info.decoder_analysis.push_back(TrackInfoField{
        .label = "Gapless preload",
        .value = "supported",
    });
    appendDeviceAnalysis(player, info);

    player.UnloadFile();
    return info;
}

}  // namespace

std::expected<TrackInfo, std::string>
VgmMetadataReader::read(const MediaSource& source,
                        MetadataReadOptions options) {
    return readVgmMetadataImpl(source, options);
}

std::expected<TrackInfo, std::string>
VgmMetadataReader::read(const std::filesystem::path& path,
                        MetadataReadOptions options) {
    return read(MediaSource::fromPath(path), options);
}
