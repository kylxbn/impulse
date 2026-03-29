#include "FileBrowser.hpp"

#include "core/SupportedFormats.hpp"

extern "C" {
#include <libavformat/avformat.h>
}

#include <algorithm>
#include <optional>
#include <system_error>

namespace {

std::optional<std::filesystem::path> canonicalDirectory(const std::filesystem::path& path) {
    std::error_code ec;
    auto canonical_path = std::filesystem::weakly_canonical(path, ec);
    if (ec || canonical_path.empty() || !std::filesystem::is_directory(canonical_path, ec))
        return std::nullopt;
    return canonical_path;
}

bool hasOnlyAudioLikeStreams(const AVFormatContext* format_context) {
    if (!format_context)
        return false;

    bool has_audio_stream = false;
    for (unsigned int i = 0; i < format_context->nb_streams; ++i) {
        const AVStream* stream = format_context->streams[i];
        if (!stream || !stream->codecpar)
            continue;

        switch (stream->codecpar->codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                has_audio_stream = true;
                break;
            case AVMEDIA_TYPE_VIDEO:
                if ((stream->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0)
                    return false;
                break;
            default:
                break;
        }
    }

    return has_audio_stream;
}

bool isFfmpegRecognisedAudioFile(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
        return false;

    AVFormatContext* format_context = nullptr;
    const std::string path_string = path.string();
    if (avformat_open_input(&format_context, path_string.c_str(), nullptr, nullptr) < 0)
        return false;

    const auto close_input = [&] {
        avformat_close_input(&format_context);
    };

    if (avformat_find_stream_info(format_context, nullptr) < 0) {
        close_input();
        return false;
    }

    const bool supported = hasOnlyAudioLikeStreams(format_context);
    close_input();
    return supported;
}

}  // namespace

// ---------------------------------------------------------------------------
FileBrowser::FileBrowser(std::filesystem::path root)
    : root_(canonicalDirectory(root).value_or(
          canonicalDirectory(std::filesystem::current_path()).value_or(std::filesystem::current_path()))),
      current_(root_) {
    refresh();
}

void FileBrowser::setRoot(std::filesystem::path root) {
    auto canonical_root = canonicalDirectory(root);
    if (!canonical_root)
        return;

    root_    = std::move(*canonical_root);
    current_ = root_;
    refresh();
}

void FileBrowser::navigate(const std::filesystem::path& dir) {
    auto canonical_dir = canonicalDirectory(dir);
    if (!canonical_dir)
        return;

    current_ = std::move(*canonical_dir);
    refresh();
}

void FileBrowser::navigateUp() {
    if (atFilesystemRoot()) return;
    navigate(current_.parent_path());
}

bool FileBrowser::atFilesystemRoot() const {
    return current_ == current_.root_path();
}

void FileBrowser::refreshCurrent() {
    refresh();
}

bool FileBrowser::isAudioFile(const std::filesystem::path& p) {
    if (isSupportedAudioFilePath(p))
        return true;

    return isFfmpegRecognisedAudioFile(p);
}

std::vector<std::filesystem::path> FileBrowser::collectAudioFiles(const std::filesystem::path& dir,
                                                                  bool recursive) {
    std::vector<std::filesystem::path> paths;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return paths;

    auto appendIfAudio = [&](const auto& entry) {
        if (!entry.is_regular_file(ec)) return;
        if (!isAudioFile(entry.path())) return;
        auto name = entry.path().filename().string();
        if (!name.empty() && name[0] == '.') return;
        paths.push_back(entry.path());
    };

    if (recursive) {
        constexpr auto options = std::filesystem::directory_options::skip_permission_denied;
        for (auto it = std::filesystem::recursive_directory_iterator(dir, options, ec);
             it != std::filesystem::recursive_directory_iterator();) {
            if (ec) {
                ec.clear();
                break;
            }
            const auto& entry = *it;
            if (entry.is_directory(ec)) {
                auto name = entry.path().filename().string();
                if (!name.empty() && name[0] == '.') {
                    it.disable_recursion_pending();
                    ec.clear();
                    continue;
                }
            }
            appendIfAudio(entry);
            it.increment(ec);
            if (ec)
                ec.clear();
        }
    } else {
        constexpr auto options = std::filesystem::directory_options::skip_permission_denied;
        for (auto it = std::filesystem::directory_iterator(dir, options, ec);
             it != std::filesystem::directory_iterator();) {
            if (ec) {
                ec.clear();
                break;
            }
            const auto& entry = *it;
            appendIfAudio(entry);
            it.increment(ec);
            if (ec)
                ec.clear();
        }
    }

    std::sort(paths.begin(), paths.end());
    return paths;
}

void FileBrowser::refresh() {
    entries_.clear();
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(current_, ec)) {
        if (ec) break;
        bool is_dir  = entry.is_directory(ec);
        bool is_file = entry.is_regular_file(ec);
        if (!is_dir && !is_file) continue;
        if (!is_dir && !isAudioFile(entry.path())) continue;
        // Skip hidden files/dirs
        auto name = entry.path().filename().string();
        if (!name.empty() && name[0] == '.') continue;

        DirEntry de;
        de.path         = entry.path();
        de.display_name = name;
        de.is_directory = is_dir;
        de.file_size    = is_file ? static_cast<int64_t>(entry.file_size(ec)) : 0;
        entries_.push_back(std::move(de));
    }

    // Directories first, then audio files; both groups sorted alphabetically
    std::sort(entries_.begin(), entries_.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.is_directory != b.is_directory) return a.is_directory > b.is_directory;
        return a.display_name < b.display_name;
    });
}
