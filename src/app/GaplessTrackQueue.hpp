#pragma once

#include "core/NowPlayingTrack.hpp"

#include <cstddef>
#include <deque>
#include <memory>

class GaplessTrackQueue {
public:
    void clear() noexcept { queue_.clear(); }

    bool empty() const noexcept { return queue_.empty(); }

    size_t size() const noexcept { return queue_.size(); }

    void enqueue(std::shared_ptr<NowPlayingTrack> track_switch) {
        queue_.push_back(std::move(track_switch));
    }

    std::shared_ptr<NowPlayingTrack> popReady(int64_t absolute_frame) {
        if (queue_.empty() || absolute_frame < queue_.front()->start_frame)
            return {};

        std::shared_ptr<NowPlayingTrack> track_switch = std::move(queue_.front());
        queue_.pop_front();
        return track_switch;
    }

private:
    std::deque<std::shared_ptr<NowPlayingTrack>> queue_;
};
