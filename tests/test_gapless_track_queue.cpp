#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "app/GaplessTrackQueue.hpp"

TEST_CASE("GaplessTrackQueue commits queued switches in boundary order") {
    GaplessTrackQueue queue;

    auto track_a = std::make_shared<NowPlayingTrack>();
    track_a->track_info = std::make_shared<TrackInfo>();
    track_a->track_info->title = "A";
    track_a->total_frames = 1000;
    track_a->start_frame = 48000;
    track_a->playlist_item_id = 11;

    auto track_b = std::make_shared<NowPlayingTrack>();
    track_b->track_info = std::make_shared<TrackInfo>();
    track_b->track_info->title = "B";
    track_b->total_frames = 2000;
    track_b->start_frame = 72000;
    track_b->playlist_item_id = 22;

    queue.enqueue(track_a);
    queue.enqueue(track_b);

    CHECK(!queue.popReady(47999));

    auto first = queue.popReady(48000);
    REQUIRE(first);
    CHECK(first->track_info->title == "A");
    CHECK(first->total_frames == 1000);
    CHECK(first->playlist_item_id == 11);

    CHECK(!queue.popReady(71999));

    auto second = queue.popReady(72000);
    REQUIRE(second);
    CHECK(second->track_info->title == "B");
    CHECK(second->total_frames == 2000);
    CHECK(second->playlist_item_id == 22);
    CHECK(queue.empty());
}
