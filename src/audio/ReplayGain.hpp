#pragma once

#include "core/TrackInfo.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>

namespace ReplayGain {

enum class GainMode {
    Track,
    Album,
    None
};

struct ReplayGainSettings {
    float preamp_with_replaygain_db = 0.0f;
    float preamp_without_replaygain_db = 0.0f;
    GainMode preferred_gain_mode = GainMode::Track;
};

inline float dbToLinear(float gain_db) {
    return std::pow(10.0f, gain_db / 20.0f);
}

inline std::optional<float> gainDbForTrack(const TrackInfo& info,
                                           const ReplayGainSettings& settings = {}) {
    if (settings.preferred_gain_mode == GainMode::None)
        return std::nullopt;

    if (settings.preferred_gain_mode == GainMode::Album) {
        if (info.rg_album_gain_db)
            return info.rg_album_gain_db;
        return info.rg_track_gain_db;
    }

    if (info.rg_track_gain_db)
        return info.rg_track_gain_db;
    return info.rg_album_gain_db;
}

inline float linearGainForTrack(const TrackInfo& info,
                                const ReplayGainSettings& settings = {}) {
    const std::optional<float> gain_db = gainDbForTrack(info, settings);

    if (!gain_db)
        return dbToLinear(settings.preamp_without_replaygain_db);

    return dbToLinear(*gain_db + settings.preamp_with_replaygain_db);
}

inline std::optional<float> plrLu(const std::optional<float>& track_gain_db,
                                  const std::optional<float>& track_peak,
                                  float reference_lufs) {
    if (!track_gain_db || !track_peak || *track_peak <= 0.0f)
        return std::nullopt;

    const float peak_dbfs = 20.0f * std::log10(*track_peak);
    const float loudness_dbfs = reference_lufs - *track_gain_db;
    return peak_dbfs - loudness_dbfs;
}

inline std::optional<float> plrLu(const TrackInfo& info, float reference_lufs) {
    return plrLu(info.rg_track_gain_db, info.rg_track_peak, reference_lufs);
}

inline void apply(std::span<float> pcm, float linear_gain) {
    if (linear_gain == 1.0f) return;

    for (float& sample : pcm)
        sample *= linear_gain;
}

}  // namespace ReplayGain
