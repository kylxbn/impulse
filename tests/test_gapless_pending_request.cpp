#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "app/GaplessPendingRequest.hpp"

TEST_CASE("GaplessPendingRequestState ignores stale schedules") {
    GaplessPendingRequestState state;

    CHECK_FALSE(state.schedule({MediaSource::fromPath("/music/next.flac"), 7, 42, 5, 3, {}}, 6, 3));
    CHECK(state.current(6, 3) == nullptr);
}

TEST_CASE("GaplessPendingRequestState discards pending requests after playlist changes") {
    GaplessPendingRequestState state;

    REQUIRE(state.schedule({MediaSource::fromPath("/music/next.flac"), 7, 42, 7, 4, {}}, 7, 4));
    REQUIRE(state.current(7, 4) != nullptr);
    CHECK(state.discardStale(8, 4));
    CHECK(state.current(8, 4) == nullptr);
}

TEST_CASE("GaplessPendingRequestState keeps matching pending requests available") {
    GaplessPendingRequestState state;

    REQUIRE(state.schedule({MediaSource::fromPath("/music/next.flac"), 7, 42, 9, 5, {}}, 9, 5));

    const GaplessPendingRequest* request = state.current(9, 5);
    REQUIRE(request != nullptr);
    CHECK(request->source == MediaSource::fromPath("/music/next.flac"));
    CHECK(request->playlist_tab_id == 7);
    CHECK(request->playlist_item_id == 42);
}

TEST_CASE("GaplessPendingRequestState replaces older pending requests") {
    GaplessPendingRequestState state;

    REQUIRE(state.schedule({MediaSource::fromPath("/music/old.flac"), 4, 1, 12, 6, {}}, 12, 6));
    REQUIRE(state.schedule({MediaSource::fromPath("/music/new.flac"), 4, 2, 12, 6, {}}, 12, 6));

    const GaplessPendingRequest* request = state.current(12, 6);
    REQUIRE(request != nullptr);
    CHECK(request->source == MediaSource::fromPath("/music/new.flac"));
    CHECK(request->playlist_tab_id == 4);
    CHECK(request->playlist_item_id == 2);
}

TEST_CASE("GaplessPendingRequestState drops requests when the playlist disappears") {
    GaplessPendingRequestState state;

    REQUIRE(state.schedule({MediaSource::fromPath("/music/next.flac"), 9, 3, 14, 7, {}}, 14, 7));
    CHECK(state.current(0, 7) == nullptr);
}

TEST_CASE("GaplessPendingRequestState discards pending requests after policy changes") {
    GaplessPendingRequestState state;

    REQUIRE(state.schedule({MediaSource::fromPath("/music/repeat.flac"), 5, 8, 22, 9, {}}, 22, 9));
    REQUIRE(state.current(22, 9) != nullptr);
    CHECK(state.discardStale(22, 10));
    CHECK(state.current(22, 10) == nullptr);
}

namespace {

std::shared_ptr<NowPlayingTrack> makeNowPlaying(uint64_t tab_id,
                                                uint64_t item_id,
                                                int64_t start_frame,
                                                bool is_stream = false) {
    auto now_playing = std::make_shared<NowPlayingTrack>();
    now_playing->track_info = std::make_shared<TrackInfo>();
    now_playing->track_info->source = MediaSource::fromPath("/music/current.flac");
    now_playing->track_info->is_stream = is_stream;
    now_playing->playlist_tab_id = tab_id;
    now_playing->playlist_item_id = item_id;
    now_playing->start_frame = start_frame;
    return now_playing;
}

GaplessPendingRequest makeRequestFollowing(uint64_t tab_id, uint64_t item_id, int64_t start_frame) {
    return GaplessPendingRequest{
        .source = MediaSource::fromPath("/music/next.flac"),
        .playlist_tab_id = tab_id,
        .playlist_item_id = item_id + 1,
        .playlist_revision = 1,
        .policy_generation = 1,
        .preceding_track = PlaybackTrackInstance{
            .playlist_tab_id = tab_id,
            .playlist_item_id = item_id,
            .start_frame = start_frame,
        },
    };
}

}  // namespace

TEST_CASE("Gapless requests are accepted only while the track they follow is audible") {
    const auto request = makeRequestFollowing(7, 42, 96'000);

    CHECK(gaplessRequestFollowsAudibleTrack(request, makeNowPlaying(7, 42, 96'000)));
    CHECK_FALSE(gaplessRequestFollowsAudibleTrack(request, makeNowPlaying(7, 43, 96'000)));
    CHECK_FALSE(gaplessRequestFollowsAudibleTrack(request, makeNowPlaying(8, 42, 96'000)));
}

TEST_CASE("Gapless requests from an earlier start of the same item are stale") {
    const auto request = makeRequestFollowing(7, 42, 96'000);

    CHECK_FALSE(gaplessRequestFollowsAudibleTrack(request, makeNowPlaying(7, 42, 192'000)));
}

TEST_CASE("Gapless requests are rejected once playback has stopped or follows a stream") {
    const auto request = makeRequestFollowing(7, 42, 0);

    CHECK_FALSE(gaplessRequestFollowsAudibleTrack(request, nullptr));
    CHECK_FALSE(gaplessRequestFollowsAudibleTrack(request, makeNowPlaying(7, 42, 0, true)));
}
