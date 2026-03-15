#include "PlaylistWorkspace.hpp"

#include <algorithm>
#include <format>
#include <ranges>
#include <utility>

namespace {

std::string sanitizePlaylistName(std::string name) {
    if (name.empty())
        return {};

    const auto first = name.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};

    const auto last = name.find_last_not_of(" \t\r\n");
    return name.substr(first, last - first + 1);
}

}  // namespace

PlaylistWorkspace::PlaylistWorkspace() {
    ensureDefaultPlaylist();
}

PlaylistDocument& PlaylistWorkspace::createPlaylist(std::string name) {
    PlaylistDocument document;
    document.id = next_playlist_id_++;
    document.name = sanitizePlaylistName(std::move(name));
    if (document.name.empty())
        document.name = defaultPlaylistName();

    playlists_.push_back(std::move(document));
    active_playlist_id_ = playlists_.back().id;
    return playlists_.back();
}

bool PlaylistWorkspace::closePlaylist(uint64_t playlist_id) {
    auto it = std::ranges::find(playlists_, playlist_id, &PlaylistDocument::id);
    if (it == playlists_.end())
        return false;

    const bool closing_active = it->id == active_playlist_id_;
    const size_t closed_index = static_cast<size_t>(std::distance(playlists_.begin(), it));
    playlists_.erase(it);

    if (playlists_.empty()) {
        active_playlist_id_ = 0;
        ensureDefaultPlaylist();
        return true;
    }

    if (closing_active) {
        const size_t next_index = std::min(closed_index, playlists_.size() - 1);
        active_playlist_id_ = playlists_[next_index].id;
    }

    return true;
}

bool PlaylistWorkspace::renamePlaylist(uint64_t playlist_id, std::string name) {
    auto* playlist = playlistById(playlist_id);
    if (!playlist)
        return false;

    std::string trimmed = sanitizePlaylistName(std::move(name));
    if (trimmed.empty())
        return false;

    playlist->name = std::move(trimmed);
    return true;
}

bool PlaylistWorkspace::activatePlaylist(uint64_t playlist_id) {
    if (!playlistById(playlist_id))
        return false;

    active_playlist_id_ = playlist_id;
    return true;
}

PlaylistDocument* PlaylistWorkspace::playlistById(uint64_t playlist_id) {
    auto it = std::ranges::find(playlists_, playlist_id, &PlaylistDocument::id);
    if (it == playlists_.end())
        return nullptr;
    return &*it;
}

const PlaylistDocument* PlaylistWorkspace::playlistById(uint64_t playlist_id) const {
    auto it = std::ranges::find(playlists_, playlist_id, &PlaylistDocument::id);
    if (it == playlists_.end())
        return nullptr;
    return &*it;
}

PlaylistDocument& PlaylistWorkspace::activePlaylist() {
    ensureDefaultPlaylist();
    return *playlistById(active_playlist_id_);
}

const PlaylistDocument& PlaylistWorkspace::activePlaylist() const {
    auto* playlist = playlistById(active_playlist_id_);
    if (!playlist)
        return playlists_.front();
    return *playlist;
}

void PlaylistWorkspace::clear() {
    playlists_.clear();
    active_playlist_id_ = 0;
}

void PlaylistWorkspace::ensureDefaultPlaylist() {
    if (!playlists_.empty()) {
        if (!playlistById(active_playlist_id_))
            active_playlist_id_ = playlists_.front().id;
        return;
    }

    createPlaylist("Playlist 1");
}

std::string PlaylistWorkspace::defaultPlaylistName() const {
    if (playlists_.empty())
        return "Playlist 1";

    return std::format("Playlist {}", playlists_.size() + 1);
}
