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

// Keep this list focused on audio-oriented extensions that FFmpeg handles well.
// Specialized formats with dedicated backends stay in their own helpers below.
constexpr auto kSupportedAudioExtensions = std::to_array<std::string_view>({
    ".aa",    ".aac",   ".aax",   ".ac3",   ".ac4",   ".adx",   ".aea",
    ".afc",   ".aif",   ".aifc",  ".aiff",  ".aix",   ".alp",   ".amr",
    ".amrnb", ".amrwb", ".ape",   ".apm",   ".aptx",  ".ast",   ".au",
    ".avr",   ".bfstm", ".binka", ".bonk",  ".brstm", ".c2",    ".caf",
    ".dfpwm", ".dff",   ".dsf",   ".dss",   ".dts",   ".dtshd", ".eac3",
    ".epaf",  ".flac",  ".fsb",   ".g722",  ".g723",  ".g726",  ".g726le",
    ".g729",  ".genh",  ".gsm",   ".hca",   ".hcom",  ".iamf",  ".ircam",
    ".kvag",  ".lc3",   ".loas",  ".m4a",   ".m4b",   ".m4r",   ".mca",
    ".mka",   ".mlp",   ".mmf",   ".mp1",   ".mp2",   ".mp3",   ".mpc",
    ".oga",   ".ogg",   ".oma",   ".opus",  ".pvf",   ".qcp",   ".qoa",
    ".spx",   ".tak",   ".tta",   ".voc",   ".wav",   ".wma",   ".wv",
});

constexpr std::array<std::string_view, 22> kTrackerModuleExtensions = {
    ".mod", ".xm",  ".it",  ".s3m", ".mptm", ".mtm",
    ".stm", ".ult", ".med", ".okt", ".669",  ".amf",
    ".ams", ".dbm", ".dmf", ".dsm", ".far",  ".mdl",
    ".mt2", ".psm", ".umx", ".mo3",
};

}  // namespace

bool isVgmExtension(std::string_view extension) {
    const std::string normalized = normalizeExtension(extension);
    return normalized == ".vgm" || normalized == ".vgz";
}

bool isSapExtension(std::string_view extension) {
    return normalizeExtension(extension) == ".sap";
}

bool isSc68Extension(std::string_view extension) {
    const std::string normalized = normalizeExtension(extension);
    return normalized == ".sc68" || normalized == ".sndh" || normalized == ".snd";
}

bool isTrackerModuleExtension(std::string_view extension) {
    const std::string normalized = normalizeExtension(extension);
    for (const auto supported : kTrackerModuleExtensions) {
        if (normalized == supported)
            return true;
    }
    return false;
}

bool isSupportedAudioExtension(std::string_view extension) {
    if (isVgmExtension(extension))
        return true;
    if (isSapExtension(extension))
        return true;
    if (isSc68Extension(extension))
        return true;
    if (isTrackerModuleExtension(extension))
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
