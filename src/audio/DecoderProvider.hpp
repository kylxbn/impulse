#pragma once

#include "audio/DecoderBackend.hpp"
#include "core/MediaSource.hpp"
#include "core/TrackInfo.hpp"
#include "metadata/MetadataReadOptions.hpp"

#include <expected>
#include <memory>
#include <string>
#include <string_view>

class DecoderProvider {
public:
    virtual ~DecoderProvider() = default;

    [[nodiscard]] virtual std::string_view name() const = 0;
    [[nodiscard]] virtual bool supportsSource(const MediaSource& source) const = 0;
    [[nodiscard]] virtual DecoderCapabilities capabilitiesForSource(const MediaSource& source) const = 0;
    [[nodiscard]] virtual std::unique_ptr<DecoderBackend> createBackend() const = 0;

    virtual std::expected<TrackInfo, std::string>
    readMetadata(const MediaSource& source,
                 MetadataReadOptions options) const = 0;

    [[nodiscard]] bool supportsGaplessPreparation(const MediaSource& source) const {
        return capabilitiesForSource(source).supports_gapless;
    }
};

[[nodiscard]] const DecoderProvider& decoderProviderForSource(const MediaSource& source);
