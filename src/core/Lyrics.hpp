#pragma once

#include "MediaSource.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

enum class LyricsSourceKind {
    None,
    EmbeddedTag,
    SidecarFile,
};

struct LyricsContent {
    std::string           text;
    LyricsSourceKind      source_kind = LyricsSourceKind::None;
    std::filesystem::path source_path;

    [[nodiscard]] bool empty() const;
};

enum class LyricsKind {
    None,
    Untimed,
    Timed,
};

struct TimedLyricLine {
    double      time_seconds = 0.0;
    std::string text;
};

struct LyricsDocument {
    LyricsKind                  kind = LyricsKind::None;
    std::vector<std::string>    untimed_lines;
    std::vector<TimedLyricLine> timed_lines;

    [[nodiscard]] bool empty() const;
};

[[nodiscard]] bool lyricsHasVisibleText(std::string_view text);
[[nodiscard]] LyricsContent selectLyricsContent(std::string_view embedded_lyrics,
                                                const MediaSource& source);
[[nodiscard]] LyricsDocument parseLyrics(std::string_view lyrics_text);
[[nodiscard]] std::optional<size_t> activeTimedLyricLineIndex(const LyricsDocument& document,
                                                              double position_seconds);
