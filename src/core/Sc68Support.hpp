#pragma once

#include <expected>
#include <filesystem>
#include <string>

std::expected<void, std::string> ensureSc68LibraryInitialized();
bool probeSc68File(const std::filesystem::path& path);

