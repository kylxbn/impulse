#include "Sc68MetadataReader.hpp"

#include "core/Sc68Support.hpp"
#include "core/SupportedFormats.hpp"

#include <sc68/sc68.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <string_view>

namespace {

constexpr int kSc68MetadataSampleRate = 48000;

using Sc68Handle = std::unique_ptr<sc68_t, decltype(&sc68_destroy)>;

bool configureSc68Instance(sc68_t* sc68,
                           std::string& error_message) {
    if (sc68_cntl(sc68, SC68_SET_PCM, SC68_PCM_S16) != 0) {
        error_message = "Cannot select libsc68 S16 PCM output";
        return false;
    }

    if (sc68_cntl(sc68, SC68_SET_OPT_STR, "ym-engine", "blep") != 0) {
        error_message = "Cannot enable libsc68 BLEP YM engine";
        return false;
    }

    if (sc68_cntl(sc68, SC68_SET_OPT_INT, "amiga-filter", 0) != 0) {
        error_message = "Cannot disable libsc68 Paula interpolation";
        return false;
    }

    return true;
}

std::string safeString(const char* value) {
    return value ? std::string(value) : std::string{};
}

std::string normalizeKey(std::string_view value) {
    std::string normalized(value);
    for (char& ch : normalized)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return normalized;
}

std::string firstNonEmpty(std::initializer_list<std::string_view> values) {
    for (const std::string_view value : values) {
        if (!value.empty())
            return std::string(value);
    }
    return {};
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
                        std::string_view label,
                        int64_t value) {
    if (value <= 0)
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

void applyKnownTag(std::string_view key,
                   std::string_view value,
                   TrackInfo&       info) {
    const std::string normalized = normalizeKey(key);
    if (normalized == "title" && info.title.empty())
        info.title = std::string(value);
    else if ((normalized == "artist" || normalized == "author") && info.artist.empty())
        info.artist = std::string(value);
    else if (normalized == "album" && info.album.empty())
        info.album = std::string(value);
    else if (normalized == "genre" && info.genre.empty())
        info.genre = std::string(value);
    else if (normalized == "year" && info.year.empty())
        info.year = std::string(value);
    else if (normalized == "comment" && info.comment.empty())
        info.comment = std::string(value);
    else if (normalized == "format" && info.codec_name.empty())
        info.codec_name = std::string(value);
}

void appendTags(sc68_t*                     sc68,
                int                         track,
                std::string_view            scope,
                std::vector<TrackInfoField>& fields,
                TrackInfo&                  info) {
    sc68_tag_t tag{};
    for (int index = 0; sc68_tag_enum(sc68, &tag, track, index, 0) == 0; ++index) {
        if (!tag.key || !tag.val || tag.val[0] == '\0')
            continue;

        applyKnownTag(tag.key, tag.val, info);
        appendField(fields,
                    std::format("{} {}", scope, tag.key),
                    tag.val);
    }
}

std::string describeHardwareFlags(const sc68_music_info_t& minfo) {
    std::vector<std::string_view> parts;
    if (minfo.trk.ym)
        parts.push_back("YM-2149");
    if (minfo.trk.ste)
        parts.push_back("STE MicroWire");
    if (minfo.trk.amiga)
        parts.push_back("Amiga Paula");
    if (minfo.trk.asid)
        parts.push_back("aSID");

    if (parts.empty())
        return {};

    std::string joined;
    for (size_t index = 0; index < parts.size(); ++index) {
        if (index > 0)
            joined += ", ";
        joined += parts[index];
    }
    return joined;
}

std::expected<TrackInfo, std::string>
readSc68MetadataImpl(const MediaSource& source,
                     MetadataReadOptions /*options*/) {
    if (!source.isFile())
        return std::unexpected(std::format("libsc68 only supports local files: {}", source.string()));

    if (!isSc68Extension(source.extension()))
        return std::unexpected(std::format("Unsupported Atari ST extension: {}", source.string()));

    if (auto init = ensureSc68LibraryInitialized(); !init)
        return std::unexpected(init.error());

    sc68_create_t create{};
    create.name = "impulse-meta";
    create.log2mem = 19;
    create.sampling_rate = kSc68MetadataSampleRate;

    Sc68Handle sc68(sc68_create(&create), &sc68_destroy);
    if (!sc68)
        return std::unexpected("Cannot create libsc68 instance");

    std::string sc68_error_message;
    if (!configureSc68Instance(sc68.get(), sc68_error_message))
        return std::unexpected(std::move(sc68_error_message));

    const std::string uri = source.path.string();
    if (sc68_load_uri(sc68.get(), uri.c_str()) != 0) {
        const char* error = sc68_error(sc68.get());
        return std::unexpected(error ? std::string(error) : "Cannot load Atari ST music");
    }

    int default_track = sc68_cntl(sc68.get(), SC68_GET_DEFTRK);
    if (default_track <= 0)
        default_track = 1;

    if (sc68_play(sc68.get(), default_track, SC68_DEF_LOOP) < 0)
        return std::unexpected("Cannot start default libsc68 track");

    if (sc68_process(sc68.get(), nullptr, nullptr) == SC68_ERROR) {
        const char* error = sc68_error(sc68.get());
        return std::unexpected(error ? std::string(error) : "Cannot prime libsc68 playback state");
    }

    sc68_music_info_t minfo{};
    if (sc68_music_info(sc68.get(), &minfo, default_track, 0) != 0)
        return std::unexpected("Cannot read libsc68 track metadata");

    TrackInfo info;
    info.source = source;
    info.decoder_name = "libsc68";
    info.path = source.path;
    info.is_stream = false;
    info.seekable = false;
    info.finite_duration = minfo.trk.time_ms > 0;
    info.duration_seconds = info.finite_duration
        ? static_cast<double>(minfo.trk.time_ms) / 1000.0
        : 0.0;
    info.sample_rate = kSc68MetadataSampleRate;
    info.bit_depth = 16;
    info.channels = 2;
    info.channel_layout = "stereo";
    info.track_number = std::to_string(default_track);
    info.title = safeString(minfo.title);
    info.artist = safeString(minfo.artist);
    info.album = safeString(minfo.album);
    info.genre = safeString(minfo.genre);
    info.year = safeString(minfo.year);
    info.codec_name = firstNonEmpty({
        safeString(minfo.format),
        "sc68",
    });
    info.container_format = source.extension();
    if (!info.container_format.empty() && info.container_format.front() == '.')
        info.container_format.erase(info.container_format.begin());

    std::error_code file_size_ec;
    info.file_size_bytes = static_cast<int64_t>(std::filesystem::file_size(source.path, file_size_ec));
    if (file_size_ec)
        info.file_size_bytes = 0;
    info.bitrate_bps = estimateBitrateBps(info.file_size_bytes, info.duration_seconds);

    appendTags(sc68.get(), 0, "Disk", info.format_metadata, info);
    appendTags(sc68.get(), default_track, "Track", info.format_metadata, info);

    info.decoder_analysis.push_back(TrackInfoField{
        .label = "Source",
        .value = source.string(),
    });
    appendIntegerField(info.decoder_analysis, "Track count", minfo.tracks);
    appendIntegerField(info.decoder_analysis, "Selected track", default_track);
    appendIntegerField(info.decoder_analysis, "Replay rate (Hz)", minfo.rate);
    appendIntegerField(info.decoder_analysis, "File size (bytes)", info.file_size_bytes);
    appendIntegerField(info.decoder_analysis, "Estimated bitrate (bps)", info.bitrate_bps);
    if (minfo.trk.time_ms > 0) {
        info.decoder_analysis.push_back(TrackInfoField{
            .label = "Track duration",
            .value = std::format("{:.3f} s", info.duration_seconds),
        });
    }
    if (minfo.dsk.time_ms > 0) {
        info.decoder_analysis.push_back(TrackInfoField{
            .label = "Disk duration",
            .value = std::format("{:.3f} s",
                                 static_cast<double>(minfo.dsk.time_ms) / 1000.0),
        });
    }
    appendField(info.decoder_analysis, "Replay routine", safeString(minfo.replay));
    appendField(info.decoder_analysis, "Hardware", safeString(minfo.trk.hw));
    appendField(info.decoder_analysis, "Hardware flags", describeHardwareFlags(minfo));
    appendField(info.decoder_analysis, "Seek support", "disabled");
    appendField(info.decoder_analysis, "Gapless preload", "not supported");
    appendField(info.decoder_analysis, "YM engine", "blep");
    appendField(info.decoder_analysis, "PCM format", "s16");
    appendField(info.decoder_analysis, "Paula interpolation", "disabled");
    appendField(info.decoder_analysis,
                "Render sample rate",
                std::format("{} Hz", kSc68MetadataSampleRate));

    return info;
}

}  // namespace

std::expected<TrackInfo, std::string>
Sc68MetadataReader::read(const MediaSource& source,
                         MetadataReadOptions options) {
    try {
        return readSc68MetadataImpl(source, options);
    } catch (const std::exception& ex) {
        return std::unexpected(ex.what());
    }
}

std::expected<TrackInfo, std::string>
Sc68MetadataReader::read(const std::filesystem::path& path,
                         MetadataReadOptions options) {
    return read(MediaSource::fromPath(path), options);
}
