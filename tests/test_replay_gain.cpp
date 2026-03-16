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

TEST_CASE("ReplayGain clip detection uses track gain and peak in track mode") {
    TrackInfo info;
    info.rg_track_gain_db = -6.0f;
    info.rg_album_gain_db = -3.0f;
    info.rg_track_peak = 0.8f;
    info.rg_album_peak = 0.4f;

    ReplayGain::ReplayGainSettings settings;
    settings.preamp_with_replaygain_db = 8.0f;

    const auto application = ReplayGain::gainApplication(info, settings);
    REQUIRE(application.replaygain_db.has_value());
    CHECK(application.source == ReplayGain::GainSource::Track);
    CHECK(*application.replaygain_db == doctest::Approx(-6.0f));
    CHECK(application.applied_gain_db == doctest::Approx(2.0f));
    REQUIRE(application.peak.has_value());
    CHECK(*application.peak == doctest::Approx(0.8f));
    REQUIRE(ReplayGain::wouldClip(info, settings).has_value());
    CHECK(*ReplayGain::wouldClip(info, settings));
}

TEST_CASE("ReplayGain clip detection uses album gain and peak in album mode") {
    TrackInfo info;
    info.rg_track_gain_db = -9.0f;
    info.rg_album_gain_db = -3.0f;
    info.rg_track_peak = 0.99f;
    info.rg_album_peak = 0.5f;

    ReplayGain::ReplayGainSettings settings;
    settings.preferred_gain_mode = ReplayGain::GainMode::Album;

    const auto application = ReplayGain::gainApplication(info, settings);
    REQUIRE(application.replaygain_db.has_value());
    CHECK(application.source == ReplayGain::GainSource::Album);
    CHECK(*application.replaygain_db == doctest::Approx(-3.0f));
    REQUIRE(application.peak.has_value());
    CHECK(*application.peak == doctest::Approx(0.5f));
    REQUIRE(ReplayGain::wouldClip(info, settings).has_value());
    CHECK_FALSE(*ReplayGain::wouldClip(info, settings));
}

TEST_CASE("ReplayGain track mode falls back to album gain and peak for clip detection") {
    TrackInfo info;
    info.rg_album_gain_db = 2.0f;
    info.rg_track_peak = 0.2f;
    info.rg_album_peak = 0.9f;

    const auto application = ReplayGain::gainApplication(info);
    REQUIRE(application.replaygain_db.has_value());
    CHECK(application.source == ReplayGain::GainSource::Album);
    CHECK(*application.replaygain_db == doctest::Approx(2.0f));
    REQUIRE(application.peak.has_value());
    CHECK(*application.peak == doctest::Approx(0.9f));
    REQUIRE(ReplayGain::wouldClip(info).has_value());
    CHECK(*ReplayGain::wouldClip(info));
}

TEST_CASE("ReplayGain album mode falls back to track gain and peak for clip detection") {
    TrackInfo info;
    info.rg_track_gain_db = 1.0f;
    info.rg_track_peak = 0.75f;
    info.rg_album_peak = 0.99f;

    ReplayGain::ReplayGainSettings settings;
    settings.preferred_gain_mode = ReplayGain::GainMode::Album;

    const auto application = ReplayGain::gainApplication(info, settings);
    REQUIRE(application.replaygain_db.has_value());
    CHECK(application.source == ReplayGain::GainSource::Track);
    CHECK(*application.replaygain_db == doctest::Approx(1.0f));
    REQUIRE(application.peak.has_value());
    CHECK(*application.peak == doctest::Approx(0.75f));
    REQUIRE(ReplayGain::wouldClip(info, settings).has_value());
    CHECK_FALSE(*ReplayGain::wouldClip(info, settings));
}

TEST_CASE("ReplayGain none mode uses preamp without ReplayGain and track peak for clip detection") {
    TrackInfo info;
    info.rg_track_gain_db = -12.0f;
    info.rg_album_gain_db = -9.0f;
    info.rg_track_peak = 0.8f;
    info.rg_album_peak = 0.2f;

    ReplayGain::ReplayGainSettings settings;
    settings.preferred_gain_mode = ReplayGain::GainMode::None;
    settings.preamp_without_replaygain_db = 2.0f;

    const auto application = ReplayGain::gainApplication(info, settings);
    CHECK(application.source == ReplayGain::GainSource::None);
    CHECK_FALSE(application.replaygain_db.has_value());
    CHECK(application.applied_gain_db == doctest::Approx(2.0f));
    REQUIRE(application.peak.has_value());
    CHECK(*application.peak == doctest::Approx(0.8f));
    REQUIRE(ReplayGain::wouldClip(info, settings).has_value());
    CHECK(*ReplayGain::wouldClip(info, settings));
}

TEST_CASE("ReplayGain clip detection returns unknown when the selected peak is missing") {
    TrackInfo info;
    info.rg_album_gain_db = 3.0f;
    info.rg_track_peak = 0.99f;

    ReplayGain::ReplayGainSettings settings;
    settings.preferred_gain_mode = ReplayGain::GainMode::Album;

    const auto application = ReplayGain::gainApplication(info, settings);
    CHECK(application.source == ReplayGain::GainSource::Album);
    CHECK_FALSE(application.peak.has_value());
    CHECK_FALSE(ReplayGain::wouldClip(info, settings).has_value());
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
