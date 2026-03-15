#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "playlist/M3U8Playlist.hpp"

#include <filesystem>
#include <fstream>

TEST_CASE("M3U8Playlist saves and loads absolute paths") {
    const std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "impulse-playlist-io";
    const std::filesystem::path playlist_path = temp_dir / "mix.m3u8";
    const std::filesystem::path first_track = temp_dir / "a.flac";
    const std::filesystem::path second_track = temp_dir / "b.mp3";

    std::filesystem::create_directories(temp_dir);
    std::ofstream(first_track).put('\n');
    std::ofstream(second_track).put('\n');

    REQUIRE(M3U8Playlist::save(playlist_path, {first_track, second_track}));

    auto loaded = M3U8Playlist::load(playlist_path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->missing_entries == 0);
    REQUIRE(loaded->entries.size() == 2);
    CHECK(loaded->entries[0] == MediaSource::fromPath(std::filesystem::weakly_canonical(first_track)));
    CHECK(loaded->entries[1] == MediaSource::fromPath(std::filesystem::weakly_canonical(second_track)));

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("M3U8Playlist resolves relative entries and counts missing files") {
    const std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "impulse-playlist-relative";
    const std::filesystem::path album_dir = temp_dir / "album";
    const std::filesystem::path playlist_path = temp_dir / "set.m3u8";
    const std::filesystem::path existing_track = album_dir / "track.flac";

    std::filesystem::create_directories(album_dir);
    std::ofstream(existing_track).put('\n');

    {
        std::ofstream out(playlist_path);
        REQUIRE(out.good());
        out << "#EXTM3U\n";
        out << "album/track.flac\n";
        out << "album/missing.flac\n";
    }

    auto loaded = M3U8Playlist::load(playlist_path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->missing_entries == 1);
    REQUIRE(loaded->entries.size() == 1);
    CHECK(loaded->entries[0] == MediaSource::fromPath(std::filesystem::weakly_canonical(existing_track)));

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("M3U8Playlist save replaces the file without leaving a temp file behind") {
    const std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "impulse-playlist-atomic";
    const std::filesystem::path playlist_path = temp_dir / "mix.m3u8";
    const std::filesystem::path temp_path = playlist_path.string() + ".tmp";
    const std::filesystem::path track_path = temp_dir / "a.flac";

    std::filesystem::create_directories(temp_dir);
    std::ofstream(track_path).put('\n');

    REQUIRE(M3U8Playlist::save(playlist_path, {track_path}));
    CHECK(std::filesystem::exists(playlist_path));
    CHECK_FALSE(std::filesystem::exists(temp_path));

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("M3U8Playlist preserves URL entries") {
    const std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "impulse-playlist-url";
    const std::filesystem::path playlist_path = temp_dir / "radio.m3u8";

    std::filesystem::create_directories(temp_dir);

    const MediaSource stream = MediaSource::fromUrl("http://example.com/stream.ogg");
    REQUIRE(M3U8Playlist::save(playlist_path, {stream}));

    auto loaded = M3U8Playlist::load(playlist_path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->missing_entries == 0);
    REQUIRE(loaded->entries.size() == 1);
    CHECK(loaded->entries[0] == stream);

    std::filesystem::remove_all(temp_dir);
}
