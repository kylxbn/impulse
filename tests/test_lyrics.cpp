#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "core/Lyrics.hpp"

#include <filesystem>
#include <fstream>

namespace {

const std::filesystem::path kTempRoot =
    std::filesystem::temp_directory_path() / "impulse_test_lyrics";

std::filesystem::path prepareTempRoot(std::string_view suite_name) {
    std::error_code cleanup_ec;
    std::filesystem::remove_all(kTempRoot, cleanup_ec);

    const auto path = kTempRoot / std::string(suite_name);
    REQUIRE(std::filesystem::create_directories(path));
    return path;
}

void writeTextFile(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out.good());
    out << text;
    REQUIRE(out.good());
}

} // namespace

TEST_CASE("Lyrics parser keeps untimed lyrics as scrollable text lines") {
    const LyricsDocument document = parseLyrics("first line\n\nthird line");

    CHECK(document.kind == LyricsKind::Untimed);
    REQUIRE(document.untimed_lines.size() == 3);
    CHECK(document.untimed_lines[0] == "first line");
    CHECK(document.untimed_lines[1].empty());
    CHECK(document.untimed_lines[2] == "third line");
}

TEST_CASE("Lyrics parser extracts timed lines and active line lookup") {
    const LyricsDocument document = parseLyrics("[00:01.50]one\n[00:05.000]two\n[00:09]three");

    CHECK(document.kind == LyricsKind::Timed);
    REQUIRE(document.timed_lines.size() == 3);
    CHECK(document.timed_lines[0].time_seconds == doctest::Approx(1.5));
    CHECK(document.timed_lines[1].time_seconds == doctest::Approx(5.0));
    CHECK(document.timed_lines[2].time_seconds == doctest::Approx(9.0));
    CHECK(activeTimedLyricLineIndex(document, 0.9) == std::nullopt);
    CHECK(activeTimedLyricLineIndex(document, 1.6) == std::optional<size_t>{0});
    CHECK(activeTimedLyricLineIndex(document, 5.0) == std::optional<size_t>{1});
    CHECK(activeTimedLyricLineIndex(document, 10.0) == std::optional<size_t>{2});
}

TEST_CASE("Lyrics parser supports multiple timestamps and offset tags") {
    const LyricsDocument document = parseLyrics("[offset:+500]\n[00:01.00][00:02.50]echo");

    CHECK(document.kind == LyricsKind::Timed);
    REQUIRE(document.timed_lines.size() == 2);
    CHECK(document.timed_lines[0].time_seconds == doctest::Approx(1.5));
    CHECK(document.timed_lines[1].time_seconds == doctest::Approx(3.0));
    CHECK(document.timed_lines[0].text == "echo");
    CHECK(document.timed_lines[1].text == "echo");
}

TEST_CASE("Lyrics parser ignores LRC metadata tags and malformed timestamp lines") {
    const LyricsDocument document = parseLyrics("[ar:Artist]\n[ti:Title]\n[00:01.00]line\n[bad]oops");

    CHECK(document.kind == LyricsKind::Timed);
    REQUIRE(document.timed_lines.size() == 1);
    CHECK(document.timed_lines[0].text == "line");
}

TEST_CASE("Embedded lyrics win over sidecar file selection") {
    const auto temp_dir = prepareTempRoot("embedded_wins");
    const auto track_path = temp_dir / "fixture.flac";
    const auto sidecar_path = temp_dir / "fixture.lrc";

    writeTextFile(track_path, "not audio, only path identity matters");
    writeTextFile(sidecar_path, "[00:01.00]sidecar");

    const LyricsContent lyrics =
        selectLyricsContent("[00:02.00]embedded", MediaSource::fromPath(track_path));

    CHECK(lyrics.source_kind == LyricsSourceKind::EmbeddedTag);
    CHECK(lyrics.source_path.empty());
    CHECK(lyrics.text == "[00:02.00]embedded");
}

TEST_CASE("Sidecar lyrics are used when embedded lyrics are blank") {
    const auto temp_dir = prepareTempRoot("sidecar_fallback");
    const auto track_path = temp_dir / "fixture.flac";
    const auto sidecar_path = temp_dir / "fixture.lrc";

    writeTextFile(track_path, "not audio, only path identity matters");
    writeTextFile(sidecar_path, "[00:01.00]sidecar");

    const LyricsContent lyrics =
        selectLyricsContent(" \n\t", MediaSource::fromPath(track_path));

    CHECK(lyrics.source_kind == LyricsSourceKind::SidecarFile);
    CHECK(lyrics.source_path == sidecar_path);
    CHECK(lyrics.text == "[00:01.00]sidecar");
}

TEST_CASE("Sidecar lookup is skipped for URL sources") {
    const LyricsContent lyrics =
        selectLyricsContent({}, MediaSource::fromUrl("https://example.com/live"));

    CHECK(lyrics.source_kind == LyricsSourceKind::None);
    CHECK(lyrics.text.empty());
}
