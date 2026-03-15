#include "MediaSource.hpp"

#include <algorithm>
#include <cctype>

namespace {

std::string trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};

    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

std::string toLower(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (unsigned char c : value)
        lowered.push_back(static_cast<char>(std::tolower(c)));
    return lowered;
}

std::string urlPathComponent(std::string_view url) {
    const size_t scheme_end = url.find(':');
    size_t path_start = std::string_view::npos;
    if (scheme_end != std::string_view::npos && url.substr(scheme_end, 3) == "://") {
        const size_t authority_start = scheme_end + 3;
        path_start = url.find('/', authority_start);
    } else {
        path_start = url.find('/');
    }

    std::string_view path = path_start == std::string_view::npos
        ? std::string_view{}
        : url.substr(path_start);
    const size_t query = path.find_first_of("?#");
    if (query != std::string_view::npos)
        path = path.substr(0, query);
    return std::string(path);
}

std::string urlLeafName(std::string_view url) {
    const std::string path = urlPathComponent(url);
    if (path.empty())
        return std::string(url);

    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos || slash + 1 >= path.size())
        return path.empty() ? std::string(url) : path;
    return path.substr(slash + 1);
}

std::string urlDirectory(std::string_view url) {
    const size_t scheme_end = url.find(':');
    size_t path_start = std::string_view::npos;
    if (scheme_end != std::string_view::npos && url.substr(scheme_end, 3) == "://") {
        const size_t authority_start = scheme_end + 3;
        path_start = url.find('/', authority_start);
    } else {
        path_start = url.find('/');
    }

    if (path_start == std::string_view::npos)
        return std::string(url) + '/';

    const size_t last_slash = url.find_last_of('/');
    if (last_slash == std::string_view::npos)
        return std::string(url);
    return std::string(url.substr(0, last_slash + 1));
}

std::string urlOrigin(std::string_view url) {
    const size_t scheme_end = url.find(':');
    if (scheme_end == std::string_view::npos || url.substr(scheme_end, 3) != "://")
        return {};

    const size_t authority_start = scheme_end + 3;
    const size_t path_start = url.find('/', authority_start);
    if (path_start == std::string_view::npos)
        return std::string(url);
    return std::string(url.substr(0, path_start));
}

std::string joinRelativeUrl(std::string_view base_url, std::string_view value) {
    if (value.empty())
        return std::string(base_url);

    if (value.starts_with("//")) {
        const size_t scheme_end = base_url.find(':');
        if (scheme_end == std::string_view::npos)
            return std::string(value);
        return std::string(base_url.substr(0, scheme_end + 1)) + std::string(value);
    }

    if (value.front() == '/')
        return urlOrigin(base_url) + std::string(value);

    return urlDirectory(base_url) + std::string(value);
}

}  // namespace

MediaSource::MediaSource(const std::filesystem::path& file_path)
    : kind(MediaSourceKind::File),
      path(file_path) {}

MediaSource MediaSource::fromPath(std::filesystem::path file_path) {
    MediaSource source;
    source.kind = MediaSourceKind::File;
    source.path = std::move(file_path);
    return source;
}

MediaSource MediaSource::fromUrl(std::string url_value) {
    MediaSource source;
    source.kind = MediaSourceKind::Url;
    source.url = trim(url_value);
    return source;
}

MediaSource MediaSource::fromSerialized(std::string_view value) {
    const std::string trimmed = trim(value);
    if (isLikelyUrl(trimmed))
        return fromUrl(trimmed);
    return fromPath(trimmed);
}

bool MediaSource::empty() const {
    if (isUrl())
        return url.empty();
    return path.empty();
}

std::string MediaSource::string() const {
    return isUrl() ? url : path.string();
}

std::string MediaSource::displayName() const {
    if (isUrl()) {
        const std::string leaf = urlLeafName(url);
        return leaf.empty() ? url : leaf;
    }

    const std::string file_name = path.filename().string();
    return file_name.empty() ? path.string() : file_name;
}

std::string MediaSource::extension() const {
    if (isUrl()) {
        const auto leaf = urlLeafName(url);
        const size_t dot = leaf.find_last_of('.');
        return dot == std::string::npos ? std::string{} : toLower(leaf.substr(dot));
    }

    return toLower(path.extension().string());
}

std::string MediaSource::stem() const {
    if (isUrl()) {
        const std::filesystem::path leaf(urlLeafName(url));
        const std::string stem_value = leaf.stem().string();
        return stem_value.empty() ? url : stem_value;
    }

    const std::string stem_value = path.stem().string();
    return stem_value.empty() ? path.filename().string() : stem_value;
}

bool isLikelyUrl(std::string_view value) {
    if (value.empty() || !std::isalpha(static_cast<unsigned char>(value.front())))
        return false;

    const size_t colon = value.find(':');
    if (colon == std::string_view::npos)
        return false;

    for (size_t i = 1; i < colon; ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (!std::isalnum(c) && c != '+' && c != '-' && c != '.')
            return false;
    }

    return true;
}

bool isPlaylistSource(const MediaSource& source) {
    const std::string ext = source.extension();
    return ext == ".m3u" || ext == ".m3u8";
}

std::optional<MediaSource> resolveMediaSourceReference(const MediaSource& base,
                                                       std::string_view value) {
    const std::string trimmed = trim(value);
    if (trimmed.empty())
        return std::nullopt;

    if (isLikelyUrl(trimmed))
        return MediaSource::fromUrl(trimmed);

    if (base.isUrl())
        return MediaSource::fromUrl(joinRelativeUrl(base.url, trimmed));

    std::filesystem::path resolved = trimmed;
    if (resolved.is_relative())
        resolved = base.path.parent_path() / resolved;

    return MediaSource::fromPath(std::move(resolved));
}
