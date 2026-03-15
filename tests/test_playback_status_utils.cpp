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
