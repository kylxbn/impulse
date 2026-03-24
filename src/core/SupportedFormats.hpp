#pragma once

#include "MediaSource.hpp"

#include <filesystem>
#include <string_view>

[[nodiscard]] bool isVgmExtension(std::string_view extension);
[[nodiscard]] bool isSc68Extension(std::string_view extension);
[[nodiscard]] bool isTrackerModuleExtension(std::string_view extension);
[[nodiscard]] bool isSupportedAudioExtension(std::string_view extension);
[[nodiscard]] bool isSupportedAudioFilePath(const std::filesystem::path& path);
