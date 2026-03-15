#pragma once

#include "core/MediaSource.hpp"

#include <cstdint>
#include <optional>

struct GaplessPendingRequest {
    MediaSource           source;
    uint64_t              playlist_tab_id = 0;
    uint64_t              playlist_item_id = 0;
    uint64_t              playlist_revision = 0;
};

class GaplessPendingRequestState {
public:
    void clear() noexcept { pending_request_.reset(); }

    bool schedule(GaplessPendingRequest request, uint64_t current_playlist_revision) {
        if (request.playlist_revision != current_playlist_revision)
            return false;

        pending_request_ = std::move(request);
        return true;
    }

    bool discardStale(uint64_t current_playlist_revision) noexcept {
        if (!pending_request_ ||
            pending_request_->playlist_revision == current_playlist_revision) {
            return false;
        }

        pending_request_.reset();
        return true;
    }

    const GaplessPendingRequest* current(uint64_t current_playlist_revision) noexcept {
        discardStale(current_playlist_revision);
        return pending_request_ ? &*pending_request_ : nullptr;
    }

    const GaplessPendingRequest* peek() const noexcept {
        return pending_request_ ? &*pending_request_ : nullptr;
    }

private:
    std::optional<GaplessPendingRequest> pending_request_;
};
