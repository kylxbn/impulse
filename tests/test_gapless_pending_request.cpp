#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "app/GaplessPendingRequest.hpp"

TEST_CASE("GaplessPendingRequestState ignores stale schedules") {
    GaplessPendingRequestState state;

    CHECK_FALSE(state.schedule({MediaSource::fromPath("/music/next.flac"), 7, 42, 5, 3}, 6, 3));
    CHECK(state.current(6, 3) == nullptr);
}

TEST_CASE("GaplessPendingRequestState discards pending requests after playlist changes") {
    GaplessPendingRequestState state;

    REQUIRE(state.schedule({MediaSource::fromPath("/music/next.flac"), 7, 42, 7, 4}, 7, 4));
    REQUIRE(state.current(7, 4) != nullptr);
    CHECK(state.discardStale(8, 4));
    CHECK(state.current(8, 4) == nullptr);
}

TEST_CASE("GaplessPendingRequestState keeps matching pending requests available") {
    GaplessPendingRequestState state;

    REQUIRE(state.schedule({MediaSource::fromPath("/music/next.flac"), 7, 42, 9, 5}, 9, 5));

    const GaplessPendingRequest* request = state.current(9, 5);
    REQUIRE(request != nullptr);
    CHECK(request->source == MediaSource::fromPath("/music/next.flac"));
    CHECK(request->playlist_tab_id == 7);
    CHECK(request->playlist_item_id == 42);
}

TEST_CASE("GaplessPendingRequestState replaces older pending requests") {
    GaplessPendingRequestState state;

    REQUIRE(state.schedule({MediaSource::fromPath("/music/old.flac"), 4, 1, 12, 6}, 12, 6));
    REQUIRE(state.schedule({MediaSource::fromPath("/music/new.flac"), 4, 2, 12, 6}, 12, 6));

    const GaplessPendingRequest* request = state.current(12, 6);
    REQUIRE(request != nullptr);
    CHECK(request->source == MediaSource::fromPath("/music/new.flac"));
    CHECK(request->playlist_tab_id == 4);
    CHECK(request->playlist_item_id == 2);
}

TEST_CASE("GaplessPendingRequestState drops requests when the playlist disappears") {
    GaplessPendingRequestState state;

    REQUIRE(state.schedule({MediaSource::fromPath("/music/next.flac"), 9, 3, 14, 7}, 14, 7));
    CHECK(state.current(0, 7) == nullptr);
}

TEST_CASE("GaplessPendingRequestState discards pending requests after policy changes") {
    GaplessPendingRequestState state;

    REQUIRE(state.schedule({MediaSource::fromPath("/music/repeat.flac"), 5, 8, 22, 9}, 22, 9));
    REQUIRE(state.current(22, 9) != nullptr);
    CHECK(state.discardStale(22, 10));
    CHECK(state.current(22, 10) == nullptr);
}
