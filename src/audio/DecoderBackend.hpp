#pragma once

#include "audio/ReplayGain.hpp"
#include "core/AudioFormat.hpp"
#include "core/MediaSource.hpp"
#include "core/TrackInfo.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct DecoderCapabilities {
    bool can_seek = false;
    bool supports_gapless = false;
};

class DecoderBackend {
public:
    virtual ~DecoderBackend() = default;

    struct OpenResult {
        bool        ok = false;
        std::string error;
    };

    // Opens a source and prepares stereo float output at the requested sample
    // rate. Backends should treat repeated open calls as a fresh session.
    virtual OpenResult open(const MediaSource& source,
                            int output_sample_rate) = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual bool isOpen() const = 0;

    // Returns decoded interleaved stereo float frames.
    // > 0: frames decoded
    //   0: clean EOF
    //  -1: unrecoverable decode error / decoder not open
    virtual int decodeNextFrames(std::vector<float>& out_pcm) = 0;
    virtual std::optional<TrackInfo> consumeTrackInfoUpdate() = 0;
    virtual bool seek(double position_seconds) = 0;
    virtual void setReplayGainSettings(ReplayGain::ReplayGainSettings settings) = 0;

    [[nodiscard]] virtual const AudioFormat& outputFormat() const = 0;
    [[nodiscard]] virtual const TrackInfo& trackInfo() const = 0;
    [[nodiscard]] virtual int64_t totalFrames() const = 0;
    [[nodiscard]] virtual int64_t instantaneousBitrateBps() const = 0;

    // Default capability reporting stays aligned with the published TrackInfo
    // contract. Override only when a backend needs stricter runtime behavior.
    [[nodiscard]] virtual DecoderCapabilities capabilities() const {
        if (!isOpen())
            return {};

        const TrackInfo& info = trackInfo();
        return DecoderCapabilities{
            .can_seek = info.seekable,
            .supports_gapless = !info.is_stream,
        };
    }
};
