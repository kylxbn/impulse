#pragma once

#include "audio/ReplayGain.hpp"

#include <filesystem>

enum class RepeatMode {
    Off,
    Playlist,
    Track
};

struct AppSettings {
    std::filesystem::path browser_root_path;
    float                 buffer_ahead_seconds = 16.0f;
    float                 replaygain_preamp_with_rg_db = 0.0f;
    float                 replaygain_preamp_without_rg_db = 0.0f;
    ReplayGain::GainMode  replaygain_mode = ReplayGain::GainMode::Track;
    RepeatMode            repeat_mode = RepeatMode::Off;
    float                 plr_reference_lufs = -18.0f;

    [[nodiscard]] ReplayGain::ReplayGainSettings replayGainSettings() const {
        return ReplayGain::ReplayGainSettings{
            .preamp_with_replaygain_db = replaygain_preamp_with_rg_db,
            .preamp_without_replaygain_db = replaygain_preamp_without_rg_db,
            .preferred_gain_mode = replaygain_mode,
        };
    }
};
