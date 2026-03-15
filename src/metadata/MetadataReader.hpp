#pragma once

#include "core/MediaSource.hpp"
#include "core/TrackInfo.hpp"

#include <expected>
#include <filesystem>
#include <string>

struct AVDictionary;
struct AVFormatContext;

class MetadataReader {
public:
    // Synchronous: opens the file, extracts all metadata, closes it.
    // Call from the decode thread before starting playback.
    static std::expected<TrackInfo, std::string>
    read(const MediaSource& source);

    static std::expected<TrackInfo, std::string>
    read(const std::filesystem::path& path);

private:
    static void parseTags(AVDictionary* dict, TrackInfo& info);
    static void parseReplayGain(AVDictionary* stream_dict,
                                AVDictionary* format_dict,
                                TrackInfo& info);
    static bool decodeAlbumArt(AVFormatContext* fmt_ctx, TrackInfo& info);
};
