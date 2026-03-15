#pragma once

#include "TrackInfo.hpp"

#include <cstdint>
#include <memory>

struct NowPlayingTrack {
    std::shared_ptr<TrackInfo> track_info;
    int64_t                    total_frames = 0;
    int64_t                    start_frame = 0;
    uint64_t                   playlist_tab_id = 0;
    uint64_t                   playlist_item_id = 0;
};
