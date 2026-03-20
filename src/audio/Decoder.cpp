#include "Decoder.hpp"

#include "audio/DecoderProvider.hpp"

namespace {

const AudioFormat& closedAudioFormat() {
    static const AudioFormat kFormat{};
    return kFormat;
}

const TrackInfo& closedTrackInfo() {
    static const TrackInfo kInfo{};
    return kInfo;
}

}  // namespace

Decoder::OpenResult Decoder::open(const MediaSource& source,
                                  int output_sample_rate) {
    close();

    const DecoderProvider& provider = decoderProviderForSource(source);
    backend_ = provider.createBackend();
    backend_->setReplayGainSettings(replay_gain_settings_);

    auto result = backend_->open(source, output_sample_rate);
    if (!result.ok)
        backend_.reset();
    return result;
}

Decoder::OpenResult Decoder::open(const std::filesystem::path& path,
                                  int output_sample_rate) {
    return open(MediaSource::fromPath(path), output_sample_rate);
}

void Decoder::close() {
    if (backend_)
        backend_->close();
    backend_.reset();
}

int Decoder::decodeNextFrames(std::vector<float>& out_pcm) {
    if (!backend_)
        return -1;
    return backend_->decodeNextFrames(out_pcm);
}

std::optional<TrackInfo> Decoder::consumeTrackInfoUpdate() {
    if (!backend_)
        return std::nullopt;
    return backend_->consumeTrackInfoUpdate();
}

bool Decoder::seek(double position_seconds) {
    if (!backend_)
        return false;
    return backend_->seek(position_seconds);
}

void Decoder::setReplayGainSettings(ReplayGain::ReplayGainSettings settings) {
    replay_gain_settings_ = settings;
    if (backend_)
        backend_->setReplayGainSettings(settings);
}

const AudioFormat& Decoder::outputFormat() const {
    return backend_ ? backend_->outputFormat() : closedAudioFormat();
}

const TrackInfo& Decoder::trackInfo() const {
    return backend_ ? backend_->trackInfo() : closedTrackInfo();
}

DecoderCapabilities Decoder::capabilities() const {
    return backend_ ? backend_->capabilities() : DecoderCapabilities{};
}

int64_t Decoder::totalFrames() const {
    return backend_ ? backend_->totalFrames() : 0;
}

int64_t Decoder::instantaneousBitrateBps() const {
    return backend_ ? backend_->instantaneousBitrateBps() : 0;
}

bool Decoder::supportsGaplessPreparation() const {
    return capabilities().supports_gapless;
}

DecoderCapabilities Decoder::capabilitiesForSource(const MediaSource& source) {
    return decoderProviderForSource(source).capabilitiesForSource(source);
}

bool Decoder::supportsGaplessForSource(const MediaSource& source) {
    return capabilitiesForSource(source).supports_gapless;
}
