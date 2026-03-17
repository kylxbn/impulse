#pragma once

#include "app/AppSettings.hpp"
#include "playlist/PlaylistManager.hpp"

#include <optional>

struct PlaybackAdvanceDecision {
    std::optional<size_t> next_index;
    bool                  handled_current_track = false;
};

inline std::optional<size_t> playbackCurrentIndex(const PlaylistManager& playlist,
                                                  uint64_t audible_playlist_item_id) {
    if (audible_playlist_item_id != 0) {
        if (const auto audible_index = playlist.indexOf(audible_playlist_item_id))
            return audible_index;
    }

    if (!playlist.hasCurrentTrack())
        return std::nullopt;

    return playlist.currentIndex();
}

inline PlaybackAdvanceDecision playbackAdvanceDecision(const PlaylistManager& playlist,
                                                       uint64_t audible_playlist_item_id,
                                                       RepeatMode repeat_mode) {
    const auto current_index = playbackCurrentIndex(playlist, audible_playlist_item_id);
    if (!current_index)
        return {};

    PlaybackAdvanceDecision decision;
    decision.handled_current_track = true;

    if (repeat_mode == RepeatMode::Track) {
        decision.next_index = *current_index;
        return decision;
    }

    const size_t next_index = *current_index + 1;
    if (next_index < playlist.size()) {
        decision.next_index = next_index;
        return decision;
    }

    if (repeat_mode == RepeatMode::Playlist && !playlist.empty())
        decision.next_index = size_t{0};

    return decision;
}

inline std::optional<size_t> playbackAdvanceIndex(const PlaylistManager& playlist,
                                                  uint64_t audible_playlist_item_id,
                                                  RepeatMode repeat_mode) {
    return playbackAdvanceDecision(playlist, audible_playlist_item_id, repeat_mode).next_index;
}
