#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

enum class MediaSourceKind {
    File,
    Url,
};

struct MediaSource {
    MediaSourceKind         kind = MediaSourceKind::File;
    std::filesystem::path   path;
    std::string             url;

    MediaSource() = default;
    MediaSource(const std::filesystem::path& file_path);

    [[nodiscard]] static MediaSource fromPath(std::filesystem::path file_path);
    [[nodiscard]] static MediaSource fromUrl(std::string url_value);
    [[nodiscard]] static MediaSource fromSerialized(std::string_view value);

    [[nodiscard]] bool isFile() const { return kind == MediaSourceKind::File; }
    [[nodiscard]] bool isUrl() const { return kind == MediaSourceKind::Url; }
    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::string string() const;
    [[nodiscard]] std::string displayName() const;
    [[nodiscard]] std::string extension() const;
    [[nodiscard]] std::string stem() const;

    bool operator==(const MediaSource& other) const = default;
};

[[nodiscard]] bool isLikelyUrl(std::string_view value);
[[nodiscard]] bool isPlaylistSource(const MediaSource& source);
[[nodiscard]] std::optional<MediaSource> resolveMediaSourceReference(const MediaSource& base,
                                                                     std::string_view value);
