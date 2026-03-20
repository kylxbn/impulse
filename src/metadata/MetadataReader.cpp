#include "MetadataReader.hpp"

#include "audio/DecoderProvider.hpp"

std::expected<TrackInfo, std::string>
MetadataReader::read(const MediaSource& source,
                     MetadataReadOptions options) {
    return decoderProviderForSource(source).readMetadata(source, options);
}

std::expected<TrackInfo, std::string>
MetadataReader::read(const std::filesystem::path& path,
                     MetadataReadOptions options) {
    return read(MediaSource::fromPath(path), options);
}
