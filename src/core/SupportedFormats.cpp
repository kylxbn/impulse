#include "SupportedFormats.hpp"

#include <array>
#include <cctype>
#include <string>

namespace {

std::string normalizeExtension(std::string_view extension) {
    std::string normalized(extension);
    for (char& ch : normalized)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return normalized;
}

constexpr std::array<std::string_view, 17> kSupportedAudioExtensions = {
    ".mp3", ".flac", ".ogg", ".opus", ".m4a", ".aac",
    ".wav", ".aiff", ".ape", ".wv", ".mpc", ".tta",
    ".wma", ".dsf", ".dff", ".tak", ".vgm",
};

}  // namespace

bool isVgmExtension(std::string_view extension) {
    const std::string normalized = normalizeExtension(extension);
    return normalized == ".vgm" || normalized == ".vgz";
}

bool isSupportedAudioExtension(std::string_view extension) {
    if (isVgmExtension(extension))
        return true;

    const std::string normalized = normalizeExtension(extension);
    for (const auto supported : kSupportedAudioExtensions) {
        if (normalized == supported)
            return true;
    }
    return false;
}

bool isSupportedAudioFilePath(const std::filesystem::path& path) {
    return isSupportedAudioExtension(path.extension().string());
}
