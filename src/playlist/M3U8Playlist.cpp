#include "M3U8Playlist.hpp"

extern "C" {
#include <libavformat/avio.h>
}

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string stripUtf8Bom(std::string line) {
    if (line.size() >= 3 &&
        static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB &&
        static_cast<unsigned char>(line[2]) == 0xBF) {
        line.erase(0, 3);
    }
    return line;
}

bool writeTextFileAtomically(const std::filesystem::path& path, const std::string& contents) {
    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), ec);

    std::filesystem::path temp_path = path;
    temp_path += ".tmp";

    {
        std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;

        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.flush();
        if (!out.good()) {
            out.close();
            std::filesystem::remove(temp_path, ec);
            return false;
        }
    }

    std::filesystem::rename(temp_path, path, ec);
    if (!ec)
        return true;

    std::filesystem::remove(temp_path, ec);
    return false;
}

std::optional<std::string> readSourceContents(const MediaSource& source) {
    if (source.isFile()) {
        std::ifstream in(source.path, std::ios::binary);
        if (!in)
            return std::nullopt;

        std::ostringstream out;
        out << in.rdbuf();
        if (!out.good())
            return std::nullopt;
        return out.str();
    }

    AVIOContext* io = nullptr;
    if (avio_open2(&io, source.url.c_str(), AVIO_FLAG_READ, nullptr, nullptr) < 0 || !io)
        return std::nullopt;

    std::string contents;
    char buffer[4096];
    while (true) {
        const int bytes_read = avio_read(io,
                                         reinterpret_cast<unsigned char*>(buffer),
                                         static_cast<int>(sizeof(buffer)));
        if (bytes_read <= 0) {
            if (bytes_read == 0 || avio_feof(io))
                break;
            avio_close(io);
            return std::nullopt;
        }
        contents.append(buffer, buffer + bytes_read);
    }

    avio_close(io);
    return contents;
}

}  // namespace

std::optional<M3U8LoadResult> M3U8Playlist::load(const MediaSource& source) {
    const auto contents = readSourceContents(source);
    if (!contents)
        return std::nullopt;

    M3U8LoadResult result;
    std::istringstream in(*contents);
    std::string line;
    bool first_line = true;
    while (std::getline(in, line)) {
        if (first_line) {
            line = stripUtf8Bom(std::move(line));
            first_line = false;
        }

        std::string trimmed = trim(std::move(line));
        if (trimmed.empty() || trimmed[0] == '#')
            continue;

        const auto resolved_source = resolveMediaSourceReference(source, trimmed);
        if (!resolved_source)
            continue;

        if (resolved_source->isFile()) {
            std::error_code ec;
            std::filesystem::path resolved = resolved_source->path;
            auto canonical = std::filesystem::weakly_canonical(resolved, ec);
            if (!ec && !canonical.empty())
                resolved = std::move(canonical);

            if (!std::filesystem::exists(resolved, ec) ||
                !std::filesystem::is_regular_file(resolved, ec)) {
                ++result.missing_entries;
                continue;
            }

            result.entries.push_back(MediaSource::fromPath(std::move(resolved)));
            continue;
        }

        result.entries.push_back(*resolved_source);
    }

    return result;
}

std::optional<M3U8LoadResult> M3U8Playlist::load(const std::filesystem::path& path) {
    return load(MediaSource::fromPath(path));
}

bool M3U8Playlist::save(const std::filesystem::path& path,
                        const std::vector<MediaSource>& track_sources) {
    std::ostringstream out;
    out << "#EXTM3U\n";
    for (const auto& source : track_sources)
        out << source.string() << '\n';

    if (!out.good())
        return false;

    return writeTextFileAtomically(path, out.str());
}
