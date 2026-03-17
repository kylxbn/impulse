#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "playlist/PlaylistManager.hpp"
#include "playlist/PlaylistWorkspace.hpp"
#include "ui/PlaybackNavigationUtils.hpp"

TEST_CASE("PlaylistManager preserves current track when moving selected rows") {
    PlaylistManager playlist;
    playlist.addTrack("/music/a.flac");
    playlist.addTrack("/music/b.flac");
    playlist.addTrack("/music/c.flac");
    playlist.addTrack("/music/d.flac");

    REQUIRE(playlist.setCurrentIndex(1));
    playlist.moveTracks({1, 2}, 4);

    REQUIRE(playlist.size() == 4);
    CHECK(playlist.currentIndex() == 2);
    CHECK(playlist.tracks()[0].file_name == "a.flac");
    CHECK(playlist.tracks()[1].file_name == "d.flac");
    CHECK(playlist.tracks()[2].file_name == "b.flac");
    CHECK(playlist.tracks()[3].file_name == "c.flac");
}

TEST_CASE("PlaylistManager insertNext inserts after the current track") {
    PlaylistManager playlist;
    playlist.addTrack("/music/a.flac");
    playlist.addTrack("/music/b.flac");
    playlist.addTrack("/music/c.flac");

    CHECK_FALSE(playlist.setCurrentIndex(0));
    playlist.insertNext("/music/x.flac");

    REQUIRE(playlist.size() == 4);
    CHECK(playlist.tracks()[0].file_name == "a.flac");
    CHECK(playlist.tracks()[1].file_name == "x.flac");
    CHECK(playlist.tracks()[2].file_name == "b.flac");
    CHECK(playlist.tracks()[3].file_name == "c.flac");
}

TEST_CASE("PlaylistManager shuffle keeps the current item selected by identity") {
    PlaylistManager playlist;
    playlist.addTrack("/music/a.flac");
    playlist.addTrack("/music/b.flac");
    playlist.addTrack("/music/c.flac");
    playlist.addTrack("/music/d.flac");
    playlist.addTrack("/music/e.flac");

    REQUIRE(playlist.setCurrentIndex(2));
    const uint64_t current_id = playlist.tracks()[playlist.currentIndex()].id;

    playlist.shuffle(1234);

    REQUIRE(playlist.size() == 5);
    REQUIRE(playlist.currentIndex() < playlist.size());
    CHECK(playlist.tracks()[playlist.currentIndex()].id == current_id);
}

TEST_CASE("PlaylistManager sort keeps the current item selected by identity") {
    PlaylistManager playlist;
    TrackInfo zebra{};
    zebra.title = "Zebra Song";
    TrackInfo alpha{};
    alpha.title = "Alpha Song";

    playlist.addTrack("/music/z.flac", &zebra);
    playlist.addTrack("/music/a.flac", &alpha);

    CHECK_FALSE(playlist.setCurrentIndex(0));
    playlist.sortBy(PlaylistSortKey::Title, PlaylistSortDirection::Ascending);

    REQUIRE(playlist.size() == 2);
    CHECK(playlist.currentIndex() == 1);
    CHECK(playlist.tracks()[0].title == "Alpha Song");
    CHECK(playlist.tracks()[1].title == "Zebra Song");
}

TEST_CASE("PlaylistManager computes PLR from the configured reference LUFS") {
    PlaylistManager playlist;
    TrackInfo info{};
    info.title = "Track";
    info.rg_track_gain_db = -6.0f;
    info.rg_track_peak = 0.5f;

    playlist.setPlrReferenceLufs(-23.0f);
    playlist.addTrack("/music/track.flac", &info);

    REQUIRE(playlist.size() == 1);
    REQUIRE(playlist.tracks()[0].plr_lu.has_value());
    CHECK(*playlist.tracks()[0].plr_lu == doctest::Approx(10.9794f).epsilon(0.0001));
}

TEST_CASE("PlaylistManager recomputes PLR when the reference LUFS changes") {
    PlaylistManager playlist;
    TrackInfo info{};
    info.title = "Track";
    info.rg_track_gain_db = -6.0f;
    info.rg_track_peak = 0.5f;

    playlist.addTrack("/music/track.flac", &info);
    REQUIRE(playlist.tracks()[0].plr_lu.has_value());
    CHECK(*playlist.tracks()[0].plr_lu == doctest::Approx(5.9794f).epsilon(0.0001));

    playlist.setPlrReferenceLufs(-23.0f);
    REQUIRE(playlist.tracks()[0].plr_lu.has_value());
    CHECK(*playlist.tracks()[0].plr_lu == doctest::Approx(10.9794f).epsilon(0.0001));
}

TEST_CASE("PlaylistManager retains track and album ReplayGain metadata") {
    PlaylistManager playlist;
    TrackInfo info{};
    info.title = "Track";
    info.rg_track_gain_db = -12.72f;
    info.rg_album_gain_db = -13.10f;
    info.rg_track_peak = 1.0f;
    info.rg_album_peak = 0.98f;

    playlist.addTrack("/music/track.wv", &info);

    REQUIRE(playlist.size() == 1);
    REQUIRE(playlist.tracks()[0].track_replay_gain_db.has_value());
    REQUIRE(playlist.tracks()[0].album_replay_gain_db.has_value());
    REQUIRE(playlist.tracks()[0].track_replay_gain_peak.has_value());
    REQUIRE(playlist.tracks()[0].album_replay_gain_peak.has_value());
    CHECK(*playlist.tracks()[0].track_replay_gain_db == doctest::Approx(-12.72f));
    CHECK(*playlist.tracks()[0].album_replay_gain_db == doctest::Approx(-13.10f));
    CHECK(*playlist.tracks()[0].track_replay_gain_peak == doctest::Approx(1.0f));
    CHECK(*playlist.tracks()[0].album_replay_gain_peak == doctest::Approx(0.98f));
}

TEST_CASE("Playback advance follows the audible playlist item before cursor sync") {
    PlaylistManager playlist;
    playlist.addTrack("/music/a.flac");
    playlist.addTrack("/music/b.flac");
    playlist.addTrack("/music/c.flac");

    CHECK_FALSE(playlist.setCurrentIndex(0));
    const uint64_t audible_item_id = playlist.tracks()[1].id;

    const auto next_index =
        playbackAdvanceIndex(playlist, audible_item_id, RepeatMode::Off);

    REQUIRE(next_index.has_value());
    CHECK(*next_index == 2);
    CHECK(playlist.tracks()[*next_index].file_name == "c.flac");
}

TEST_CASE("Playback advance falls back to the playlist cursor when no audible item is known") {
    PlaylistManager playlist;
    playlist.addTrack("/music/a.flac");
    playlist.addTrack("/music/b.flac");

    CHECK_FALSE(playlist.setCurrentIndex(0));

    const auto next_index = playbackAdvanceIndex(playlist, 0, RepeatMode::Off);

    REQUIRE(next_index.has_value());
    CHECK(*next_index == 1);
    CHECK(playlist.tracks()[*next_index].file_name == "b.flac");
}

TEST_CASE("Playback advance still marks end-of-track handled at the end of the playlist") {
    PlaylistManager playlist;
    playlist.addTrack("/music/a.flac");
    playlist.addTrack("/music/b.flac");

    REQUIRE(playlist.setCurrentIndex(1));

    const PlaybackAdvanceDecision advance =
        playbackAdvanceDecision(playlist, playlist.tracks()[1].id, RepeatMode::Off);

    CHECK(advance.handled_current_track);
    CHECK_FALSE(advance.next_index.has_value());
}

TEST_CASE("PlaylistWorkspace keeps at least one playlist and switches the active tab") {
    PlaylistWorkspace workspace;

    REQUIRE(workspace.playlists().size() == 1);
    CHECK(workspace.activePlaylist().name == "Playlist 1");

    auto& playlist = workspace.createPlaylist("Favorites");
    CHECK(workspace.playlists().size() == 2);
    CHECK(workspace.activePlaylistId() == playlist.id);

    REQUIRE(workspace.closePlaylist(playlist.id));
    CHECK(workspace.playlists().size() == 1);
    CHECK(workspace.activePlaylist().name == "Playlist 1");
}

TEST_CASE("PlaylistWorkspace trims playlist names and rejects empty renames") {
    PlaylistWorkspace workspace;

    auto& playlist = workspace.createPlaylist("Temp");
    REQUIRE(workspace.renamePlaylist(playlist.id, "  Chill Mix  "));
    CHECK(playlist.name == "Chill Mix");
    CHECK_FALSE(workspace.renamePlaylist(playlist.id, "   "));
}
