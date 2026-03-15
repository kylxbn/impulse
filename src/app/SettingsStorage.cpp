#include "SettingsStorage.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

namespace {

const char* replayGainModeToString(ReplayGain::GainMode mode) {
    switch (mode) {
        case ReplayGain::GainMode::None:  return "none";
        case ReplayGain::GainMode::Album: return "album";
        case ReplayGain::GainMode::Track:
        default:                          return "track";
    }
}

std::optional<ReplayGain::GainMode> replayGainModeFromString(const std::string& value) {
    if (value == "track") return ReplayGain::GainMode::Track;
    if (value == "album") return ReplayGain::GainMode::Album;
    if (value == "none") return ReplayGain::GainMode::None;
    return std::nullopt;
}

const char* repeatModeToString(RepeatMode mode) {
    switch (mode) {
        case RepeatMode::Playlist: return "playlist";
        case RepeatMode::Track:    return "track";
        case RepeatMode::Off:
        default:                   return "off";
    }
}

std::optional<RepeatMode> repeatModeFromString(const std::string& value) {
    if (value == "off") return RepeatMode::Off;
    if (value == "playlist") return RepeatMode::Playlist;
    if (value == "track") return RepeatMode::Track;
    return std::nullopt;
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

}  // namespace

bool SettingsStorage::save(const std::filesystem::path& path, const AppSettings& settings) {
    std::ostringstream out;
    out << "browser_root " << std::quoted(settings.browser_root_path.string()) << '\n';
    out << "buffer_ahead_seconds " << settings.buffer_ahead_seconds << '\n';
    out << "replaygain_with_rg " << settings.replaygain_preamp_with_rg_db << '\n';
    out << "replaygain_without_rg " << settings.replaygain_preamp_without_rg_db << '\n';
    out << "replaygain_mode " << replayGainModeToString(settings.replaygain_mode) << '\n';
    out << "repeat_mode " << repeatModeToString(settings.repeat_mode) << '\n';
    out << "plr_reference_lufs " << settings.plr_reference_lufs << '\n';
    if (!out.good())
        return false;

    return writeTextFileAtomically(path, out.str());
}

AppSettings SettingsStorage::loadOrDefault(const std::filesystem::path& path,
                                           const AppSettings& defaults) {
    std::ifstream in(path);
    if (!in) return defaults;

    AppSettings settings = defaults;
    std::string key;
    while (in >> key) {
        if (key == "browser_root") {
            std::string value;
            if (!(in >> std::quoted(value))) return defaults;
            settings.browser_root_path = value;
        } else if (key == "buffer_ahead_seconds") {
            if (!(in >> settings.buffer_ahead_seconds)) return defaults;
        } else if (key == "replaygain_with_rg") {
            if (!(in >> settings.replaygain_preamp_with_rg_db)) return defaults;
        } else if (key == "replaygain_without_rg") {
            if (!(in >> settings.replaygain_preamp_without_rg_db)) return defaults;
        } else if (key == "replaygain_mode") {
            std::string value;
            if (!(in >> value)) return defaults;
            auto mode = replayGainModeFromString(value);
            if (!mode) return defaults;
            settings.replaygain_mode = *mode;
        } else if (key == "repeat_mode") {
            std::string value;
            if (!(in >> value)) return defaults;
            auto mode = repeatModeFromString(value);
            if (!mode) return defaults;
            settings.repeat_mode = *mode;
        } else if (key == "plr_reference_lufs") {
            if (!(in >> settings.plr_reference_lufs)) return defaults;
        } else {
            std::string ignored;
            std::getline(in, ignored);
        }
    }

    return settings;
}
