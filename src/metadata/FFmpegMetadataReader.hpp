#pragma once

#include "core/MediaSource.hpp"
#include "core/TrackInfo.hpp"
#include "metadata/MetadataReadOptions.hpp"

#include <expected>
#include <filesystem>
#include <string>

struct AVDictionary;
struct AVFormatContext;

class FFmpegMetadataReader {
public:
    static std::expected<TrackInfo, std::string>
    read(const MediaSource& source,
         MetadataReadOptions options = {});

    static std::expected<TrackInfo, std::string>
    read(const std::filesystem::path& path,
         MetadataReadOptions options = {});

private:
    static void parseTags(AVDictionary* dict, TrackInfo& info);
    static void parseReplayGain(AVDictionary* stream_dict,
                                AVDictionary* format_dict,
                                TrackInfo& info);
    static bool decodeAlbumArt(AVFormatContext* fmt_ctx, TrackInfo& info);
};
