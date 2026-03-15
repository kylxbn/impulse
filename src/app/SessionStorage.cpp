#include "SessionStorage.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

namespace {

bool writeTextFileAtomically(const std::filesystem::path& path, const std::string& contents) {
    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), ec);

    std::filesystem::path temp_path = path;
    temp_path += ".tmp";

    {
        std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;

        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.flush();
        if (!out.good()) {
            out.close();
            std::filesystem::remove(temp_path, ec);
            return false;
        }
    }

    std::filesystem::rename(temp_path, path, ec);
    if (!ec)
        return true;

    std::filesystem::remove(temp_path, ec);
    return false;
}

}  // namespace

bool SessionStorage::save(const std::filesystem::path& path, const SessionState& state) {
    std::ostringstream out;
    out << "browser " << std::quoted(state.browser_path.string()) << '\n';
    out << "active " << state.active_playlist_index << '\n';
    for (const auto& playlist : state.playlists) {
        out << "playlist " << std::quoted(playlist.name) << ' ' << playlist.current_index << '\n';
        for (const auto& source : playlist.track_sources)
            out << "source " << std::quoted(source.string()) << '\n';
    }
    if (!out.good())
        return false;

    return writeTextFileAtomically(path, out.str());
}

std::optional<SessionState> SessionStorage::load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return std::nullopt;

    SessionState state;
    std::vector<MediaSource> legacy_track_sources;
    size_t legacy_current_index = 0;
    SessionPlaylistState* current_playlist = nullptr;
    std::string key;
    while (in >> key) {
        if (key == "browser") {
            std::string value;
            if (!(in >> std::quoted(value))) return std::nullopt;
            state.browser_path = value;
        } else if (key == "active") {
            if (!(in >> state.active_playlist_index)) return std::nullopt;
        } else if (key == "playlist") {
            SessionPlaylistState playlist;
            if (!(in >> std::quoted(playlist.name))) return std::nullopt;
            if (!(in >> playlist.current_index)) return std::nullopt;
            state.playlists.push_back(std::move(playlist));
            current_playlist = &state.playlists.back();
        } else if (key == "current") {
            if (!(in >> legacy_current_index)) return std::nullopt;
        } else if (key == "volume") {
            float ignored = 0.0f;
            if (!(in >> ignored)) return std::nullopt;
        } else if (key == "source" || key == "track") {
            std::string value;
            if (!(in >> std::quoted(value))) return std::nullopt;
            const MediaSource source = MediaSource::fromSerialized(value);
            if (current_playlist)
                current_playlist->track_sources.push_back(source);
            else
                legacy_track_sources.push_back(source);
        } else {
            std::string ignored;
            std::getline(in, ignored);
        }
    }

    if (state.playlists.empty() && (!legacy_track_sources.empty() || legacy_current_index > 0)) {
        state.playlists.push_back(SessionPlaylistState{
            .name = "Playlist 1",
            .track_sources = std::move(legacy_track_sources),
            .current_index = legacy_current_index,
        });
        state.active_playlist_index = 0;
    }

    return state;
}
