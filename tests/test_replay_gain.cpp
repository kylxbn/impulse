#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "audio/ReplayGain.hpp"

#include <array>

TEST_CASE("ReplayGain prefers track gain when available") {
    TrackInfo info;
    info.rg_track_gain_db = -6.0f;
    info.rg_album_gain_db = 3.0f;
    ReplayGain::ReplayGainSettings settings;
    settings.preamp_with_replaygain_db = 2.0f;

    CHECK(ReplayGain::linearGainForTrack(info, settings) ==
          doctest::Approx(0.630957f).epsilon(0.0001));
}

TEST_CASE("ReplayGain can prefer album gain when requested") {
    TrackInfo info;
    info.rg_track_gain_db = -6.0f;
    info.rg_album_gain_db = -3.0f;
    ReplayGain::ReplayGainSettings settings;
    settings.preferred_gain_mode = ReplayGain::GainMode::Album;
    settings.preamp_with_replaygain_db = -1.0f;

    CHECK(ReplayGain::linearGainForTrack(info, settings) ==
          doctest::Approx(0.630957f).epsilon(0.0001));
}

TEST_CASE("ReplayGain falls back to album gain") {
    TrackInfo info;
    info.rg_album_gain_db = -3.0f;
    ReplayGain::ReplayGainSettings settings;
    settings.preamp_with_replaygain_db = -1.0f;

    CHECK(ReplayGain::linearGainForTrack(info, settings) ==
          doctest::Approx(0.630957f).epsilon(0.0001));
}

TEST_CASE("ReplayGain album mode falls back to track gain") {
    TrackInfo info;
    info.rg_track_gain_db = -3.0f;
    ReplayGain::ReplayGainSettings settings;
    settings.preferred_gain_mode = ReplayGain::GainMode::Album;
    settings.preamp_with_replaygain_db = -1.0f;

    CHECK(ReplayGain::linearGainForTrack(info, settings) ==
          doctest::Approx(0.630957f).epsilon(0.0001));
}

TEST_CASE("ReplayGain uses separate preamp for tracks without ReplayGain") {
    TrackInfo info;
    ReplayGain::ReplayGainSettings settings;
    settings.preamp_without_replaygain_db = -4.0f;

    CHECK(ReplayGain::linearGainForTrack(info, settings) ==
          doctest::Approx(0.630957f).epsilon(0.0001));
}

TEST_CASE("ReplayGain none mode ignores ReplayGain tags") {
    TrackInfo info;
    info.rg_track_gain_db = -6.0f;
    info.rg_album_gain_db = -3.0f;
    ReplayGain::ReplayGainSettings settings;
    settings.preferred_gain_mode = ReplayGain::GainMode::None;
    settings.preamp_with_replaygain_db = 8.0f;
    settings.preamp_without_replaygain_db = -4.0f;

    CHECK(ReplayGain::linearGainForTrack(info, settings) ==
          doctest::Approx(0.630957f).epsilon(0.0001));
}

TEST_CASE("ReplayGain computes PLR from configurable reference LUFS") {
    TrackInfo info;
    info.rg_track_gain_db = -6.0f;
    info.rg_track_peak = 0.5f;

    auto plr = ReplayGain::plrLu(info, -23.0f);
    REQUIRE(plr.has_value());
    CHECK(*plr == doctest::Approx(10.9794f).epsilon(0.0001));
}

TEST_CASE("ReplayGain scales samples in place") {
    std::array<float, 4> pcm{0.5f, -0.5f, 0.25f, -0.25f};

    ReplayGain::apply(pcm, 0.5f);

    CHECK(pcm[0] == doctest::Approx(0.25f));
    CHECK(pcm[1] == doctest::Approx(-0.25f));
    CHECK(pcm[2] == doctest::Approx(0.125f));
    CHECK(pcm[3] == doctest::Approx(-0.125f));
}
