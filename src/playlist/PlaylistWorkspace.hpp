#pragma once

#include "playlist/PlaylistManager.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct PlaylistDocument {
    uint64_t        id = 0;
    std::string     name;
    PlaylistManager playlist;
    uint64_t        revision = 1;
};

class PlaylistWorkspace {
public:
    PlaylistWorkspace();

    PlaylistDocument& createPlaylist(std::string name = {});
    bool closePlaylist(uint64_t playlist_id);
    bool renamePlaylist(uint64_t playlist_id, std::string name);
    bool activatePlaylist(uint64_t playlist_id);

    PlaylistDocument*       playlistById(uint64_t playlist_id);
    const PlaylistDocument* playlistById(uint64_t playlist_id) const;

    PlaylistDocument&       activePlaylist();
    const PlaylistDocument& activePlaylist() const;
    uint64_t                activePlaylistId() const { return active_playlist_id_; }

    const std::vector<PlaylistDocument>& playlists() const { return playlists_; }
    std::vector<PlaylistDocument>&       playlists() { return playlists_; }

    void clear();
    void ensureDefaultPlaylist();

private:
    std::string defaultPlaylistName() const;

    std::vector<PlaylistDocument> playlists_;
    uint64_t                      active_playlist_id_ = 0;
    uint64_t                      next_playlist_id_ = 1;
};
