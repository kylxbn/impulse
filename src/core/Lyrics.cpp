#include "core/Lyrics.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <tuple>

namespace {

std::string_view trimAsciiLeft(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    return text;
}

std::string_view trimBom(std::string_view text) {
    static constexpr char kUtf8Bom[] = "\xEF\xBB\xBF";
    if (text.size() >= 3 && text.substr(0, 3) == std::string_view(kUtf8Bom, 3))
        text.remove_prefix(3);
    return text;
}

std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t newline = text.find('\n', start);
        const size_t end = newline == std::string_view::npos ? text.size() : newline;
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (lines.empty())
            line = trimBom(line);
        lines.push_back(line);
        if (newline == std::string_view::npos)
            break;
        start = newline + 1;
    }

    if (lines.empty())
        lines.push_back({});
    return lines;
}

bool isMetadataTag(std::string_view tag) {
    const size_t colon = tag.find(':');
    if (colon == std::string_view::npos || colon == 0)
        return false;

    for (char ch : tag.substr(0, colon)) {
        if (!std::isalpha(static_cast<unsigned char>(ch)))
            return false;
    }
    return true;
}

std::optional<double> parseTimestampTag(std::string_view tag) {
    const size_t colon = tag.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 >= tag.size())
        return std::nullopt;

    int minutes = 0;
    const auto minutes_result = std::from_chars(tag.data(), tag.data() + colon, minutes);
    if (minutes_result.ec != std::errc{} || minutes < 0)
        return std::nullopt;

    std::string_view seconds_part = tag.substr(colon + 1);
    const size_t dot = seconds_part.find('.');
    const std::string_view whole_seconds_part =
        dot == std::string_view::npos ? seconds_part : seconds_part.substr(0, dot);
    if (whole_seconds_part.empty())
        return std::nullopt;

    int seconds = 0;
    const auto seconds_result = std::from_chars(whole_seconds_part.data(),
                                                whole_seconds_part.data() + whole_seconds_part.size(),
                                                seconds);
    if (seconds_result.ec != std::errc{} || seconds < 0 || seconds >= 60)
        return std::nullopt;

    double fraction = 0.0;
    if (dot != std::string_view::npos) {
        const std::string_view fraction_part = seconds_part.substr(dot + 1);
        if (fraction_part.empty())
            return std::nullopt;
        double scale = 0.1;
        for (char ch : fraction_part) {
            if (!std::isdigit(static_cast<unsigned char>(ch)))
                return std::nullopt;
            fraction += static_cast<double>(ch - '0') * scale;
            scale *= 0.1;
        }
    }

    return static_cast<double>(minutes * 60 + seconds) + fraction;
}

std::optional<double> parseOffsetTag(std::string_view tag) {
    constexpr std::string_view kPrefix = "offset:";
    if (tag.size() <= kPrefix.size())
        return std::nullopt;

    for (size_t i = 0; i < kPrefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(tag[i])) != kPrefix[i])
            return std::nullopt;
    }

    size_t value_start = kPrefix.size();
    int sign = 1;
    if (tag[value_start] == '+' || tag[value_start] == '-') {
        sign = tag[value_start] == '-' ? -1 : 1;
        ++value_start;
    }
    if (value_start >= tag.size())
        return std::nullopt;

    int offset_ms = 0;
    const auto result = std::from_chars(tag.data() + value_start,
                                        tag.data() + tag.size(),
                                        offset_ms);
    if (result.ec != std::errc{})
        return std::nullopt;

    return static_cast<double>(sign * offset_ms) / 1000.0;
}

std::optional<std::string> readTextFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return std::nullopt;

    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

} // namespace

bool LyricsContent::empty() const {
    return text.empty();
}

bool LyricsDocument::empty() const {
    return kind == LyricsKind::None ||
           (untimed_lines.empty() && timed_lines.empty());
}

bool lyricsHasVisibleText(std::string_view text) {
    text = trimBom(text);
    for (char ch : text) {
        if (!std::isspace(static_cast<unsigned char>(ch)))
            return true;
    }
    return false;
}

LyricsContent selectLyricsContent(std::string_view embedded_lyrics,
                                  const MediaSource& source) {
    if (lyricsHasVisibleText(embedded_lyrics)) {
        return LyricsContent{
            .text = std::string(embedded_lyrics),
            .source_kind = LyricsSourceKind::EmbeddedTag,
            .source_path = {},
        };
    }

    if (!source.isFile())
        return {};

    const std::filesystem::path sidecar_path =
        source.path.parent_path() / std::filesystem::path(source.path.stem().string() + ".lrc");
    std::error_code ec;
    if (!std::filesystem::is_regular_file(sidecar_path, ec))
        return {};

    const auto sidecar_text = readTextFile(sidecar_path);
    if (!sidecar_text || !lyricsHasVisibleText(*sidecar_text))
        return {};

    return LyricsContent{
        .text = *sidecar_text,
        .source_kind = LyricsSourceKind::SidecarFile,
        .source_path = sidecar_path,
    };
}

LyricsDocument parseLyrics(std::string_view lyrics_text) {
    lyrics_text = trimBom(lyrics_text);
    if (!lyricsHasVisibleText(lyrics_text))
        return {};

    LyricsDocument document;
    std::vector<std::string> untimed_lines;
    std::vector<TimedLyricLine> timed_lines;
    double offset_seconds = 0.0;

    for (std::string_view line : splitLines(lyrics_text)) {
        size_t position = 0;
        std::vector<double> line_timestamps;
        bool handled_as_metadata = false;

        while (position < line.size() && line[position] == '[') {
            const size_t closing = line.find(']', position + 1);
            if (closing == std::string_view::npos)
                break;

            const std::string_view tag = line.substr(position + 1, closing - position - 1);
            if (const auto timestamp = parseTimestampTag(tag)) {
                line_timestamps.push_back(*timestamp);
                position = closing + 1;
                continue;
            }

            if (const auto offset = parseOffsetTag(tag)) {
                offset_seconds = *offset;
                handled_as_metadata = true;
                position = closing + 1;
                continue;
            }

            if (isMetadataTag(tag)) {
                handled_as_metadata = true;
                position = closing + 1;
                continue;
            }

            break;
        }

        if (!line_timestamps.empty()) {
            const std::string_view text = trimAsciiLeft(line.substr(position));
            for (double timestamp : line_timestamps) {
                timed_lines.push_back(TimedLyricLine{
                    .time_seconds = std::max(0.0, timestamp + offset_seconds),
                    .text = std::string(text),
                });
            }
            continue;
        }

        if (handled_as_metadata && position >= line.size())
            continue;

        untimed_lines.emplace_back(line);
    }

    if (!timed_lines.empty()) {
        std::sort(timed_lines.begin(), timed_lines.end(),
                  [](const TimedLyricLine& lhs, const TimedLyricLine& rhs) {
                      return std::tie(lhs.time_seconds, lhs.text) <
                             std::tie(rhs.time_seconds, rhs.text);
                  });
        document.kind = LyricsKind::Timed;
        document.timed_lines = std::move(timed_lines);
        return document;
    }

    if (!untimed_lines.empty()) {
        document.kind = LyricsKind::Untimed;
        document.untimed_lines = std::move(untimed_lines);
    }

    return document;
}

std::optional<size_t> activeTimedLyricLineIndex(const LyricsDocument& document,
                                                double position_seconds) {
    if (document.kind != LyricsKind::Timed || document.timed_lines.empty())
        return std::nullopt;

    const auto it = std::upper_bound(document.timed_lines.begin(),
                                     document.timed_lines.end(),
                                     position_seconds,
                                     [](double position, const TimedLyricLine& line) {
                                         return position < line.time_seconds;
                                     });
    if (it == document.timed_lines.begin())
        return std::nullopt;

    return static_cast<size_t>(std::distance(document.timed_lines.begin(), std::prev(it)));
}
