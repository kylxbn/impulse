#pragma once

#include "core/MediaSource.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct SessionPlaylistState {
    std::string              name;
    std::vector<MediaSource> track_sources;
    size_t                   current_index = 0;
};

struct SessionState {
    std::filesystem::path          browser_path;
    std::vector<SessionPlaylistState> playlists;
    size_t                         active_playlist_index = 0;
};

class SessionStorage {
public:
    static bool save(const std::filesystem::path& path, const SessionState& state);
    static std::optional<SessionState> load(const std::filesystem::path& path);
};
