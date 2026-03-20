#pragma once

#include "audio/DecoderBackend.hpp"
#include "core/AudioFormat.hpp"
#include "core/MediaSource.hpp"
#include "core/TrackInfo.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class Decoder {
public:
    Decoder() = default;
    ~Decoder() = default;

    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    using OpenResult = DecoderBackend::OpenResult;

    OpenResult open(const MediaSource& source,
                    int output_sample_rate = 48000);
    OpenResult open(const std::filesystem::path& path,
                    int output_sample_rate = 48000);

    void close();

    [[nodiscard]] bool is_open() const { return backend_ && backend_->isOpen(); }
    int decodeNextFrames(std::vector<float>& out_pcm);
    std::optional<TrackInfo> consumeTrackInfoUpdate();
    bool seek(double position_seconds);
    void setReplayGainSettings(ReplayGain::ReplayGainSettings settings);

    [[nodiscard]] const AudioFormat& outputFormat() const;
    [[nodiscard]] const TrackInfo& trackInfo() const;
    [[nodiscard]] DecoderCapabilities capabilities() const;
    [[nodiscard]] int64_t totalFrames() const;
    [[nodiscard]] int64_t instantaneousBitrateBps() const;
    [[nodiscard]] bool supportsGaplessPreparation() const;
    [[nodiscard]] static DecoderCapabilities capabilitiesForSource(const MediaSource& source);
    [[nodiscard]] static bool supportsGaplessForSource(const MediaSource& source);

private:
    std::unique_ptr<DecoderBackend> backend_;
    ReplayGain::ReplayGainSettings replay_gain_settings_;
};
