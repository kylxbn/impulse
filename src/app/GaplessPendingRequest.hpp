#pragma once

#include "core/MediaSource.hpp"
#include "core/NowPlayingTrack.hpp"
#include "core/PlaybackStatusUtils.hpp"

#include <cstdint>
#include <memory>
#include <optional>

struct GaplessPendingRequest {
    MediaSource           source;
    uint64_t              playlist_tab_id = 0;
    uint64_t              playlist_item_id = 0;
    uint64_t              playlist_revision = 0;
    uint64_t              policy_generation = 0;
    PlaybackTrackInstance preceding_track;
};

// A gapless request is only meaningful while the track it was scheduled
// after is still the audible one, and only when that track can end cleanly.
inline bool gaplessRequestFollowsAudibleTrack(const GaplessPendingRequest& request,
                                              const std::shared_ptr<NowPlayingTrack>& now_playing) {
    if (!now_playing || !now_playing->track_info)
        return false;
    if (now_playing->track_info->is_stream)
        return false;
    return playbackTrackInstanceMatches(request.preceding_track, now_playing);
}

class GaplessPendingRequestState {
public:
    void clear() noexcept { pending_request_.reset(); }

    bool schedule(GaplessPendingRequest request,
                  uint64_t current_playlist_revision,
                  uint64_t current_policy_generation) {
        if (request.playlist_revision != current_playlist_revision ||
            request.policy_generation != current_policy_generation)
            return false;

        pending_request_ = std::move(request);
        return true;
    }

    bool discardStale(uint64_t current_playlist_revision,
                      uint64_t current_policy_generation) noexcept {
        if (!pending_request_ ||
            (pending_request_->playlist_revision == current_playlist_revision &&
             pending_request_->policy_generation == current_policy_generation)) {
            return false;
        }

        pending_request_.reset();
        return true;
    }

    const GaplessPendingRequest* current(uint64_t current_playlist_revision,
                                         uint64_t current_policy_generation) noexcept {
        discardStale(current_playlist_revision, current_policy_generation);
        return pending_request_ ? &*pending_request_ : nullptr;
    }

    const GaplessPendingRequest* peek() const noexcept {
        return pending_request_ ? &*pending_request_ : nullptr;
    }

private:
    std::optional<GaplessPendingRequest> pending_request_;
};
