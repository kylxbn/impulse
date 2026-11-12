#pragma once

#include "core/NowPlayingTrack.hpp"

#include <cstddef>
#include <memory>
#include <optional>

#include "core/PlaybackState.hpp"

struct PlaybackTrackInstance {
    uint64_t playlist_tab_id = 0;
    uint64_t playlist_item_id = 0;
    int64_t  start_frame = 0;

    bool operator==(const PlaybackTrackInstance&) const = default;
};

inline PlaybackStatus resumeStatusAfterSuccessfulSeek(PlaybackStatus previous_status) {
    if (previous_status == PlaybackStatus::EndOfTrack)
        return PlaybackStatus::Playing;

    return previous_status;
}

// Once the current track has fully drained, the decode thread reports
// EndOfTrack while it still holds a gapless request/preload and Stopped when
// it has nothing queued. The UI must open the next track itself in the latter
// case, and also when the transition was never eligible for gapless.
inline bool shouldOpenEndOfTrackTargetDirectly(PlaybackStatus status, bool can_gapless) {
    if (status == PlaybackStatus::Stopped)
        return true;
    if (status == PlaybackStatus::EndOfTrack)
        return !can_gapless;
    return false;
}

inline bool isIdlePlaybackStatus(PlaybackStatus status) {
    return status == PlaybackStatus::Stopped ||
           status == PlaybackStatus::EndOfTrack ||
           status == PlaybackStatus::Error;
}

inline bool shouldPublishAudibleEndOfTrack(bool decoder_reached_eof,
                                           size_t buffered_samples,
                                           bool has_prepared_gapless_track,
                                           bool has_pending_pcm) {
    return decoder_reached_eof &&
           buffered_samples == 0 &&
           !has_prepared_gapless_track &&
           !has_pending_pcm;
}

inline std::optional<PlaybackTrackInstance> playbackTrackInstance(
    const std::shared_ptr<NowPlayingTrack>& now_playing) {
    if (!now_playing)
        return std::nullopt;

    return PlaybackTrackInstance{
        .playlist_tab_id = now_playing->playlist_tab_id,
        .playlist_item_id = now_playing->playlist_item_id,
        .start_frame = now_playing->start_frame,
    };
}

inline bool playbackTrackInstanceMatches(
    const std::optional<PlaybackTrackInstance>& expected,
    const std::shared_ptr<NowPlayingTrack>& now_playing) {
    return expected == playbackTrackInstance(now_playing);
}
