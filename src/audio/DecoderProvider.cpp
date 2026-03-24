#include "audio/DecoderProvider.hpp"

#include "audio/FFmpegDecoderBackend.hpp"
#include "audio/OpenMptDecoderBackend.hpp"
#include "audio/Sc68DecoderBackend.hpp"
#include "audio/VgmDecoderBackend.hpp"

#include <array>
#include <format>
#include <stdexcept>

namespace {

const auto& decoderProviders() {
    static const std::array<const DecoderProvider*, 4> kProviders = {
        &vgmDecoderProvider(),
        &sc68DecoderProvider(),
        &openMptDecoderProvider(),
        &ffmpegDecoderProvider(),
    };
    return kProviders;
}

}  // namespace

const DecoderProvider& decoderProviderForSource(const MediaSource& source) {
    for (const DecoderProvider* provider : decoderProviders()) {
        if (provider && provider->supportsSource(source))
            return *provider;
    }

    throw std::runtime_error(std::format("No decoder provider for source: {}", source.string()));
}
