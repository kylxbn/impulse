#pragma once

#include "core/MediaSource.hpp"
#include "audio/ReplayGain.hpp"

#include <variant>

// ---------------------------------------------------------------------------
// Commands: posted by UI thread, consumed by decode thread
// ---------------------------------------------------------------------------

struct CommandOpenFile  {
    MediaSource           source;
    uint64_t              playlist_tab_id = 0;
    uint64_t              playlist_item_id = 0;
};
struct CommandOpenFileGapless {
    MediaSource           source;
    uint64_t              playlist_tab_id = 0;
    uint64_t              playlist_item_id = 0;
    uint64_t              playlist_revision = 0;
};
struct CommandPlay      {};
struct CommandPause     {};
struct CommandStop      {};
struct CommandSeek      { double position_seconds; };
struct CommandSetVolume { float linear_gain; };
struct CommandSetReplayGainSettings { ReplayGain::ReplayGainSettings settings; };
struct CommandNext      {};
struct CommandPrev      {};
struct CommandQuit      {};

using Command = std::variant<
    CommandOpenFile,
    CommandOpenFileGapless,
    CommandPlay,
    CommandPause,
    CommandStop,
    CommandSeek,
    CommandSetVolume,
    CommandSetReplayGainSettings,
    CommandNext,
    CommandPrev,
    CommandQuit
>;
