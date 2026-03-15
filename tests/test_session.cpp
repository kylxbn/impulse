#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "app/SessionStorage.hpp"

#include <filesystem>
#include <fstream>

TEST_CASE("SessionStorage round-trips browser, playlists, and current index") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "impulse-session-test.txt";
    std::filesystem::remove(path);

    SessionState state;
    state.browser_path = "/music/library";
    state.playlists = {
        SessionPlaylistState{
            .name = "Playlist 1",
            .track_sources = {
                MediaSource::fromPath("/music/library/a.flac"),
                MediaSource::fromPath("/music/library/b.flac")
            },
            .current_index = 1,
        },
        SessionPlaylistState{
            .name = "Chill",
            .track_sources = {
                MediaSource::fromPath("/music/library/c.flac")
            },
            .current_index = 0,
        }
    };
    state.active_playlist_index = 1;

    REQUIRE(SessionStorage::save(path, state));

    auto loaded = SessionStorage::load(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->browser_path == state.browser_path);
    REQUIRE(loaded->playlists.size() == 2);
    CHECK(loaded->playlists[0].name == "Playlist 1");
    CHECK(loaded->playlists[0].track_sources == state.playlists[0].track_sources);
    CHECK(loaded->playlists[0].current_index == 1);
    CHECK(loaded->playlists[1].name == "Chill");
    CHECK(loaded->active_playlist_index == 1);

    std::filesystem::remove(path);
}

TEST_CASE("SessionStorage tolerates legacy volume entries") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "impulse-session-test-legacy.txt";
    std::filesystem::remove(path);

    {
        std::ofstream out(path);
        REQUIRE(out.good());
        out << "browser \"/music/library\"\n";
        out << "current 1\n";
        out << "volume 0.75\n";
        out << "track \"/music/library/a.flac\"\n";
        out << "track \"/music/library/b.flac\"\n";
    }

    auto loaded = SessionStorage::load(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->browser_path == "/music/library");
    CHECK(loaded->active_playlist_index == 0);
    REQUIRE(loaded->playlists.size() == 1);
    CHECK(loaded->playlists[0].name == "Playlist 1");
    CHECK(loaded->playlists[0].current_index == 1);
    REQUIRE(loaded->playlists[0].track_sources.size() == 2);
    CHECK(loaded->playlists[0].track_sources[0] == MediaSource::fromPath("/music/library/a.flac"));
    CHECK(loaded->playlists[0].track_sources[1] == MediaSource::fromPath("/music/library/b.flac"));

    std::filesystem::remove(path);
}

TEST_CASE("SessionStorage save replaces the file without leaving a temp file behind") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "impulse-session-atomic.txt";
    const std::filesystem::path temp_path = path.string() + ".tmp";
    std::filesystem::remove(path);
    std::filesystem::remove(temp_path);

    SessionState state;
    state.browser_path = "/music/library";
    state.playlists = {
        SessionPlaylistState{
            .name = "Playlist 1",
            .track_sources = {MediaSource::fromPath("/music/library/a.flac")},
            .current_index = 0,
        }
    };

    REQUIRE(SessionStorage::save(path, state));
    CHECK(std::filesystem::exists(path));
    CHECK_FALSE(std::filesystem::exists(temp_path));

    std::filesystem::remove(path);
}

TEST_CASE("SessionStorage round-trips URL playlist entries") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "impulse-session-test-url.txt";
    std::filesystem::remove(path);

    SessionState state;
    state.playlists = {
        SessionPlaylistState{
            .name = "Radio",
            .track_sources = {MediaSource::fromUrl("http://example.com/live.ogg")},
            .current_index = 0,
        }
    };

    REQUIRE(SessionStorage::save(path, state));

    auto loaded = SessionStorage::load(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->playlists.size() == 1);
    REQUIRE(loaded->playlists[0].track_sources.size() == 1);
    CHECK(loaded->playlists[0].track_sources[0] == MediaSource::fromUrl("http://example.com/live.ogg"));

    std::filesystem::remove(path);
}
