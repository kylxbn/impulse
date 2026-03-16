#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "core/PlaybackStatusUtils.hpp"

TEST_CASE("Seeking from end-of-track resumes playback") {
    CHECK(resumeStatusAfterSuccessfulSeek(PlaybackStatus::EndOfTrack) ==
          PlaybackStatus::Playing);
}

TEST_CASE("Seeking preserves paused playback") {
    CHECK(resumeStatusAfterSuccessfulSeek(PlaybackStatus::Paused) ==
          PlaybackStatus::Paused);
}

TEST_CASE("Seeking preserves stopped playback state") {
    CHECK(resumeStatusAfterSuccessfulSeek(PlaybackStatus::Stopped) ==
          PlaybackStatus::Stopped);
}

TEST_CASE("Audible end-of-track waits for buffered audio to drain") {
    CHECK_FALSE(shouldPublishAudibleEndOfTrack(true, 256, false, false));
    CHECK_FALSE(shouldPublishAudibleEndOfTrack(true, 0, true, false));
    CHECK_FALSE(shouldPublishAudibleEndOfTrack(true, 0, false, true));
    CHECK(shouldPublishAudibleEndOfTrack(true, 0, false, false));
}

TEST_CASE("Playback track instances distinguish repeated starts of the same playlist item") {
    auto first_pass = std::make_shared<NowPlayingTrack>();
    first_pass->playlist_tab_id = 7;
    first_pass->playlist_item_id = 42;
    first_pass->start_frame = 12'000;

    auto second_pass = std::make_shared<NowPlayingTrack>(*first_pass);
    second_pass->start_frame = 24'000;

    const auto scheduled = playbackTrackInstance(first_pass);
    CHECK(playbackTrackInstanceMatches(scheduled, first_pass));
    CHECK_FALSE(playbackTrackInstanceMatches(scheduled, second_pass));
}

TEST_CASE("Playback track instance matching treats missing now playing as a distinct state") {
    auto now_playing = std::make_shared<NowPlayingTrack>();
    now_playing->playlist_tab_id = 3;
    now_playing->playlist_item_id = 9;
    now_playing->start_frame = 480;

    CHECK(playbackTrackInstanceMatches(std::nullopt, nullptr));
    CHECK_FALSE(playbackTrackInstanceMatches(std::nullopt, now_playing));
}
