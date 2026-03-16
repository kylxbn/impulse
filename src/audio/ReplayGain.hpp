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

enum class GainSource {
    Track,
    Album,
    None
};

struct ReplayGainSettings {
    float preamp_with_replaygain_db = 0.0f;
    float preamp_without_replaygain_db = 0.0f;
    GainMode preferred_gain_mode = GainMode::Track;
};

struct SelectedGain {
    GainSource           source = GainSource::None;
    std::optional<float> gain_db;
};

struct GainApplication {
    GainSource           source = GainSource::None;
    std::optional<float> replaygain_db;
    float                applied_gain_db = 0.0f;
    float                linear_gain = 1.0f;
    std::optional<float> peak;
};

inline float dbToLinear(float gain_db) {
    return std::pow(10.0f, gain_db / 20.0f);
}

inline SelectedGain selectGain(const std::optional<float>& track_gain_db,
                               const std::optional<float>& album_gain_db,
                               GainMode preferred_gain_mode) {
    if (preferred_gain_mode == GainMode::None)
        return {};

    if (preferred_gain_mode == GainMode::Album) {
        if (album_gain_db)
            return SelectedGain{.source = GainSource::Album, .gain_db = album_gain_db};
        if (track_gain_db)
            return SelectedGain{.source = GainSource::Track, .gain_db = track_gain_db};
        return {};
    }

    if (track_gain_db)
        return SelectedGain{.source = GainSource::Track, .gain_db = track_gain_db};
    if (album_gain_db)
        return SelectedGain{.source = GainSource::Album, .gain_db = album_gain_db};
    return {};
}

inline SelectedGain selectGain(const TrackInfo& info,
                               const ReplayGainSettings& settings = {}) {
    return selectGain(info.rg_track_gain_db,
                      info.rg_album_gain_db,
                      settings.preferred_gain_mode);
}

inline std::optional<float> gainDbForTrack(const TrackInfo& info,
                                           const ReplayGainSettings& settings = {}) {
    return selectGain(info, settings).gain_db;
}

inline std::optional<float> peakForGainSource(const std::optional<float>& track_peak,
                                              const std::optional<float>& album_peak,
                                              GainSource source) {
    switch (source) {
        case GainSource::Track:
            return track_peak;
        case GainSource::Album:
            return album_peak;
        case GainSource::None:
            return track_peak;
    }

    return std::nullopt;
}

inline std::optional<float> peakForGainSource(const TrackInfo& info,
                                              GainSource source) {
    return peakForGainSource(info.rg_track_peak, info.rg_album_peak, source);
}

inline GainApplication gainApplication(const std::optional<float>& track_gain_db,
                                       const std::optional<float>& album_gain_db,
                                       const std::optional<float>& track_peak,
                                       const std::optional<float>& album_peak,
                                       const ReplayGainSettings& settings = {}) {
    const SelectedGain selected = selectGain(track_gain_db,
                                             album_gain_db,
                                             settings.preferred_gain_mode);

    GainApplication application;
    application.source = selected.source;
    application.replaygain_db = selected.gain_db;

    if (selected.gain_db) {
        application.applied_gain_db = *selected.gain_db + settings.preamp_with_replaygain_db;
        application.peak = peakForGainSource(track_peak, album_peak, selected.source);
    } else {
        application.applied_gain_db = settings.preamp_without_replaygain_db;
        application.peak = track_peak;
    }

    application.linear_gain = dbToLinear(application.applied_gain_db);
    return application;
}

inline GainApplication gainApplication(const TrackInfo& info,
                                       const ReplayGainSettings& settings = {}) {
    return gainApplication(info.rg_track_gain_db,
                           info.rg_album_gain_db,
                           info.rg_track_peak,
                           info.rg_album_peak,
                           settings);
}

inline std::optional<bool> wouldClip(const GainApplication& application) {
    if (!application.peak || *application.peak <= 0.0f)
        return std::nullopt;

    return *application.peak * application.linear_gain > 1.0f;
}

inline std::optional<bool> wouldClip(const std::optional<float>& track_gain_db,
                                     const std::optional<float>& album_gain_db,
                                     const std::optional<float>& track_peak,
                                     const std::optional<float>& album_peak,
                                     const ReplayGainSettings& settings = {}) {
    return wouldClip(gainApplication(track_gain_db,
                                     album_gain_db,
                                     track_peak,
                                     album_peak,
                                     settings));
}

inline std::optional<bool> wouldClip(const TrackInfo& info,
                                     const ReplayGainSettings& settings = {}) {
    return wouldClip(gainApplication(info, settings));
}

inline float linearGainForTrack(const TrackInfo& info,
                                const ReplayGainSettings& settings = {}) {
    return gainApplication(info, settings).linear_gain;
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
