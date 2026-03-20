#pragma once

#include "core/MediaSource.hpp"
#include "core/TrackInfo.hpp"
#include "metadata/MetadataReadOptions.hpp"

#include <expected>
#include <filesystem>
#include <string>

class VgmMetadataReader {
public:
    static std::expected<TrackInfo, std::string>
    read(const MediaSource& source,
         MetadataReadOptions options = {});

    static std::expected<TrackInfo, std::string>
    read(const std::filesystem::path& path,
         MetadataReadOptions options = {});
};
