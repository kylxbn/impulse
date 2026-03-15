#pragma once

#include "app/AppSettings.hpp"

#include <filesystem>

class SettingsStorage {
public:
    static bool save(const std::filesystem::path& path, const AppSettings& settings);
    static AppSettings loadOrDefault(const std::filesystem::path& path,
                                     const AppSettings& defaults);
};
