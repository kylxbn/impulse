#include "MprisModel.hpp"

#include "browser/FileBrowser.hpp"
#include "core/MediaSource.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <system_error>

namespace {

constexpr std::string_view kTrackPathPrefix = "/com/impulse/track";

uint64_t fnv1a64(std::string_view value) {
    constexpr uint64_t kOffsetBasis = 14695981039346656037ull;
    constexpr uint64_t kPrime = 1099511628211ull;

    uint64_t hash = kOffsetBasis;
    for (unsigned char c : value) {
        hash ^= static_cast<uint64_t>(c);
        hash *= kPrime;
    }
    return hash;
}

std::string percentEncode(std::string_view value) {
    std::string encoded;
    encoded.reserve(value.size());
    for (unsigned char c : value) {
        const bool is_unreserved =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~' || c == '/';
        if (is_unreserved) {
            encoded.push_back(static_cast<char>(c));
            continue;
        }

        encoded += std::format("%{:02X}", static_cast<unsigned int>(c));
    }
    return encoded;
}

std::optional<std::string> percentDecode(std::string_view value) {
    auto hex_value = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    std::string decoded;
    decoded.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '%') {
            decoded.push_back(value[i]);
            continue;
        }

        if (i + 2 >= value.size())
            return std::nullopt;

        const int hi = hex_value(value[i + 1]);
        const int lo = hex_value(value[i + 2]);
        if (hi < 0 || lo < 0)
            return std::nullopt;

        decoded.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
    }

    return decoded;
}

std::optional<int32_t> parseTrackNumber(std::string_view value) {
    size_t prefix_len = 0;
    while (prefix_len < value.size() && std::isdigit(static_cast<unsigned char>(value[prefix_len])))
        ++prefix_len;
    if (prefix_len == 0)
        return std::nullopt;

    int32_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = begin + prefix_len;
    const auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end)
        return std::nullopt;

    return parsed;
}

std::filesystem::path bestEffortAbsolutePath(const std::filesystem::path& path) {
    if (path.empty())
        return {};

    std::error_code ec;
    const auto absolute = std::filesystem::absolute(path, ec);
    return ec ? path : absolute;
}

}  // namespace

const char* playbackStatusToMprisString(PlaybackStatus status) {
    switch (status) {
        case PlaybackStatus::Paused:
            return "Paused";
        case PlaybackStatus::Buffering:
        case PlaybackStatus::Playing:
        case PlaybackStatus::Seeking:
            return "Playing";
        case PlaybackStatus::EndOfTrack:
        case PlaybackStatus::Error:
        case PlaybackStatus::Stopped:
        default:
            return "Stopped";
    }
}

MprisLoopStatus repeatModeToMprisLoopStatus(RepeatMode mode) {
    switch (mode) {
        case RepeatMode::Track:
            return MprisLoopStatus::Track;
        case RepeatMode::Playlist:
            return MprisLoopStatus::Playlist;
        case RepeatMode::Off:
        default:
            return MprisLoopStatus::None;
    }
}

RepeatMode mprisLoopStatusToRepeatMode(MprisLoopStatus status) {
    switch (status) {
        case MprisLoopStatus::Track:
            return RepeatMode::Track;
        case MprisLoopStatus::Playlist:
            return RepeatMode::Playlist;
        case MprisLoopStatus::None:
        default:
            return RepeatMode::Off;
    }
}

const char* mprisLoopStatusToString(MprisLoopStatus status) {
    switch (status) {
        case MprisLoopStatus::Track:
            return "Track";
        case MprisLoopStatus::Playlist:
            return "Playlist";
        case MprisLoopStatus::None:
        default:
            return "None";
    }
}

std::optional<MprisLoopStatus> parseMprisLoopStatus(std::string_view value) {
    if (value == "None")
        return MprisLoopStatus::None;
    if (value == "Track")
        return MprisLoopStatus::Track;
    if (value == "Playlist")
        return MprisLoopStatus::Playlist;
    return std::nullopt;
}

std::string buildMprisTrackId(const NowPlayingTrack& track) {
    if (track.playlist_tab_id != 0 && track.playlist_item_id != 0) {
        return std::format("{}/playlist_{}/item_{}",
                           kTrackPathPrefix,
                           track.playlist_tab_id,
                           track.playlist_item_id);
    }

    const auto path_string = track.track_info
        ? track.track_info->source.string()
        : std::string{};
    const uint64_t hash = fnv1a64(path_string);
    return std::format("{}/file_{:016x}", kTrackPathPrefix, hash);
}

MprisTrackMetadata buildMprisTrackMetadata(const NowPlayingTrack& track,
                                           uint32_t sample_rate) {
    MprisTrackMetadata metadata;
    metadata.track_id = buildMprisTrackId(track);

    if (!track.track_info)
        return metadata;

    const auto& info = *track.track_info;
    metadata.url = info.source.isUrl() ? info.source.url : filePathToUri(info.path);
    if (!info.external_album_art_path.empty())
        metadata.art_url = filePathToUri(info.external_album_art_path);
    metadata.title = info.title.empty()
        ? info.source.displayName()
        : info.title;
    if (!info.artist.empty())
        metadata.artists.push_back(info.artist);
    metadata.album = info.album;
    if (!info.genre.empty())
        metadata.genres.push_back(info.genre);

    if (sample_rate > 0 && track.total_frames > 0) {
        metadata.length_us = static_cast<int64_t>(
            (static_cast<long double>(track.total_frames) * 1'000'000.0L) /
            static_cast<long double>(sample_rate));
    }

    if (const auto track_number = parseTrackNumber(info.track_number)) {
        metadata.track_number = *track_number;
        metadata.has_track_number = true;
    }

    return metadata;
}

std::string filePathToUri(const std::filesystem::path& path) {
    const std::filesystem::path absolute = bestEffortAbsolutePath(path);
    return std::format("file://{}", percentEncode(absolute.generic_string()));
}

std::optional<std::filesystem::path> fileUriToPath(std::string_view uri) {
    constexpr std::string_view kScheme = "file://";
    if (!uri.starts_with(kScheme))
        return std::nullopt;

    std::string_view remainder = uri.substr(kScheme.size());
    std::string_view authority;
    std::string_view encoded_path;

    if (!remainder.empty() && remainder.front() != '/') {
        const size_t slash = remainder.find('/');
        if (slash == std::string_view::npos)
            return std::nullopt;
        authority = remainder.substr(0, slash);
        encoded_path = remainder.substr(slash);
    } else {
        encoded_path = remainder;
    }

    if (!authority.empty() && authority != "localhost")
        return std::nullopt;

    const auto decoded = percentDecode(encoded_path);
    if (!decoded || decoded->empty())
        return std::nullopt;

    return std::filesystem::path(*decoded);
}

bool isSupportedOpenUri(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) &&
           !ec &&
           FileBrowser::isAudioFile(path);
}

std::optional<MediaSource> mediaSourceFromOpenUri(std::string_view uri) {
    if (const auto path = fileUriToPath(uri); path && isSupportedOpenUri(*path))
        return MediaSource::fromPath(*path);

    if (!isLikelyUrl(uri))
        return std::nullopt;

    const MediaSource source = MediaSource::fromUrl(std::string(uri));
    return source.isUrl() ? std::optional<MediaSource>{source} : std::nullopt;
}

const std::vector<std::string>& supportedMimeTypes() {
    static const std::vector<std::string> kMimeTypes = {
        "audio/aac",
        "audio/flac",
        "audio/mp4",
        "audio/mpeg",
        "audio/ogg",
        "audio/opus",
        "audio/vnd.wave",
        "audio/wav",
        "audio/x-aiff",
        "audio/x-ms-wma",
    };
    return kMimeTypes;
}

const std::vector<std::string>& supportedUriSchemes() {
    static const std::vector<std::string> kSchemes = {"file", "http", "https"};
    return kSchemes;
}

int64_t secondsToMprisTime(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0)
        return 0;

    constexpr double kScale = 1'000'000.0;
    const double clamped = std::min(seconds, static_cast<double>(std::numeric_limits<int64_t>::max()) / kScale);
    return static_cast<int64_t>(std::llround(clamped * kScale));
}

double mprisTimeToSeconds(int64_t usec) {
    if (usec <= 0)
        return 0.0;
    return static_cast<double>(usec) / 1'000'000.0;
}
