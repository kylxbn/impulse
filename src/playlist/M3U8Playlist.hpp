#pragma once

#include "core/MediaSource.hpp"

#include <filesystem>
#include <optional>
#include <vector>

struct M3U8LoadResult {
    std::vector<MediaSource> entries;
    size_t                   missing_entries = 0;
};

class M3U8Playlist {
public:
    static std::optional<M3U8LoadResult> load(const MediaSource& source);
    static std::optional<M3U8LoadResult> load(const std::filesystem::path& path);
    static bool save(const std::filesystem::path& path,
                     const std::vector<MediaSource>& track_sources);
};
