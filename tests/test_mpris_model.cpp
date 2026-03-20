#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "app/AppSettings.hpp"
#include "core/NowPlayingTrack.hpp"
#include "core/TrackInfo.hpp"
#include "mpris/MprisModel.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

TEST_CASE("Playback status maps to MPRIS states") {
    CHECK(std::string(playbackStatusToMprisString(PlaybackStatus::Stopped)) == "Stopped");
    CHECK(std::string(playbackStatusToMprisString(PlaybackStatus::Buffering)) == "Playing");
    CHECK(std::string(playbackStatusToMprisString(PlaybackStatus::Playing)) == "Playing");
    CHECK(std::string(playbackStatusToMprisString(PlaybackStatus::Paused)) == "Paused");
    CHECK(std::string(playbackStatusToMprisString(PlaybackStatus::Seeking)) == "Playing");
    CHECK(std::string(playbackStatusToMprisString(PlaybackStatus::EndOfTrack)) == "Stopped");
    CHECK(std::string(playbackStatusToMprisString(PlaybackStatus::Error)) == "Stopped");
}

TEST_CASE("Repeat mode round-trips through MPRIS loop status") {
    CHECK(repeatModeToMprisLoopStatus(RepeatMode::Off) == MprisLoopStatus::None);
    CHECK(repeatModeToMprisLoopStatus(RepeatMode::Playlist) == MprisLoopStatus::Playlist);
    CHECK(repeatModeToMprisLoopStatus(RepeatMode::Track) == MprisLoopStatus::Track);

    CHECK(mprisLoopStatusToRepeatMode(MprisLoopStatus::None) == RepeatMode::Off);
    CHECK(mprisLoopStatusToRepeatMode(MprisLoopStatus::Playlist) == RepeatMode::Playlist);
    CHECK(mprisLoopStatusToRepeatMode(MprisLoopStatus::Track) == RepeatMode::Track);
    CHECK(parseMprisLoopStatus("Playlist") == MprisLoopStatus::Playlist);
    CHECK_FALSE(parseMprisLoopStatus("Invalid").has_value());
}

TEST_CASE("Track metadata derives stable identifiers and xesam fields") {
    auto track_info = std::make_shared<TrackInfo>();
    track_info->source = MediaSource::fromPath("/music/example.flac");
    track_info->path = "/music/example.flac";
    track_info->external_album_art_path = "/music/Cover.png";
    track_info->title = "Example";
    track_info->artist = "Impulse";
    track_info->album = "Album";
    track_info->genre = "Jazz";
    track_info->track_number = "3/12";

    NowPlayingTrack track;
    track.track_info = track_info;
    track.total_frames = 44100 * 180;
    track.playlist_tab_id = 7;
    track.playlist_item_id = 11;

    const auto metadata = buildMprisTrackMetadata(track, 44100);
    const std::vector<std::string> expected_artists = {"Impulse"};
    const std::vector<std::string> expected_genres = {"Jazz"};
    CHECK(metadata.track_id == "/com/impulse/track/playlist_7/item_11");
    CHECK(metadata.url == "file:///music/example.flac");
    CHECK(metadata.art_url == "file:///music/Cover.png");
    CHECK(metadata.title == "Example");
    CHECK(metadata.artists == expected_artists);
    CHECK(metadata.album == "Album");
    CHECK(metadata.genres == expected_genres);
    CHECK(metadata.length_us == 180000000);
    CHECK(metadata.has_track_number);
    CHECK(metadata.track_number == 3);
}

TEST_CASE("File URIs round-trip and supported files are validated") {
    const auto temp_dir = std::filesystem::temp_directory_path() / "impulse-mpris-test";
    std::filesystem::create_directories(temp_dir);
    const auto temp_file = temp_dir / "track with spaces.mp3";
    const auto vgm_file = temp_dir / "track.vgm";

    {
        std::ofstream stream(temp_file);
        stream << "test";
    }
    {
        std::ofstream stream(vgm_file);
        stream << "test";
    }

    const std::string uri = filePathToUri(temp_file);
    CHECK(uri.find("file://") == 0);

    const auto decoded = fileUriToPath(uri);
    REQUIRE(decoded.has_value());
    CHECK(*decoded == temp_file);
    CHECK(isSupportedOpenUri(temp_file));
    CHECK(isSupportedOpenUri(vgm_file));
    const auto remote = mediaSourceFromOpenUri("https://example.com/track.mp3");
    REQUIRE(remote.has_value());
    CHECK(remote->isUrl());

    std::filesystem::remove(temp_file);
    std::filesystem::remove(vgm_file);
    std::filesystem::remove(temp_dir);
}
