#pragma once

#include <cstddef>

#include "core/PlaybackState.hpp"

inline PlaybackStatus resumeStatusAfterSuccessfulSeek(PlaybackStatus previous_status) {
    if (previous_status == PlaybackStatus::EndOfTrack)
        return PlaybackStatus::Playing;

    return previous_status;
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
