#include "Sc68DecoderBackend.hpp"

#include "audio/ReplayGain.hpp"
#include "core/Sc68Support.hpp"
#include "core/SupportedFormats.hpp"
#include "metadata/Sc68MetadataReader.hpp"

#include <sc68/sc68.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <ranges>
#include <span>
#include <string_view>

namespace {

constexpr float kSc68SampleScale = 1.0f / 32768.0f;

bool configureSc68Instance(sc68_t* sc68,
                           std::string& error_message) {
    if (sc68_cntl(sc68, SC68_SET_PCM, SC68_PCM_S16) != 0) {
        error_message = "Cannot select libsc68 S16 PCM output";
        return false;
    }

    if (sc68_cntl(sc68, SC68_SET_OPT_STR, "ym-engine", "blep") != 0) {
        error_message = "Cannot enable libsc68 BLEP YM engine";
        return false;
    }

    if (sc68_cntl(sc68, SC68_SET_OPT_INT, "amiga-filter", 0) != 0) {
        error_message = "Cannot disable libsc68 Paula interpolation";
        return false;
    }

    return true;
}

void upsertAnalysisField(std::vector<TrackInfoField>& fields,
                         std::string_view label,
                         std::string value) {
    const auto it = std::ranges::find(fields, label, &TrackInfoField::label);
    if (it != fields.end()) {
        it->value = std::move(value);
        return;
    }

    fields.push_back(TrackInfoField{
        .label = std::string(label),
        .value = std::move(value),
    });
}

class Sc68DecoderProvider final : public DecoderProvider {
public:
    [[nodiscard]] std::string_view name() const override {
        return "libsc68";
    }

    [[nodiscard]] bool supportsSource(const MediaSource& source) const override {
        if (!source.isFile() || !isSc68Extension(source.extension()))
            return false;

        if (source.extension() == ".snd" || source.extension() == ".SND")
            return probeSc68File(source.path);

        return true;
    }

    [[nodiscard]] DecoderCapabilities capabilitiesForSource(const MediaSource& /*source*/) const override {
        return DecoderCapabilities{
            .can_seek = false,
            .supports_gapless = false,
        };
    }

    [[nodiscard]] std::unique_ptr<DecoderBackend> createBackend() const override {
        return std::make_unique<Sc68DecoderBackend>();
    }

    std::expected<TrackInfo, std::string>
    readMetadata(const MediaSource& source,
                 MetadataReadOptions options) const override {
        return Sc68MetadataReader::read(source, options);
    }
};

}  // namespace

Sc68DecoderBackend::Sc68DecoderBackend() = default;

Sc68DecoderBackend::~Sc68DecoderBackend() {
    close();
}

DecoderBackend::OpenResult Sc68DecoderBackend::open(const MediaSource& source,
                                                    int output_sample_rate) {
    close();

    if (!source.isFile())
        return {false, "libsc68 only supports local files"};

    if (!isSc68Extension(source.extension()))
        return {false, std::format("Unsupported Atari ST file: {}", source.string())};

    if (auto init = ensureSc68LibraryInitialized(); !init)
        return {false, init.error()};

    sc68_create_t create{};
    create.name = "impulse";
    create.log2mem = 19;
    create.sampling_rate = static_cast<unsigned int>(std::max(output_sample_rate, 1));

    sc68_ = sc68_create(&create);
    if (!sc68_)
        return {false, "Cannot create libsc68 decoder instance"};

    std::string sc68_error_message;
    if (!configureSc68Instance(sc68_, sc68_error_message)) {
        close();
        return {false, std::move(sc68_error_message)};
    }

    const std::string uri = source.path.string();
    if (sc68_load_uri(sc68_, uri.c_str()) != 0) {
        const char* error = sc68_error(sc68_);
        close();
        return {false, error ? std::string(error) : "Cannot load Atari ST music"};
    }

    int selected_track = sc68_cntl(sc68_, SC68_GET_DEFTRK);
    if (selected_track <= 0)
        selected_track = 1;

    if (sc68_play(sc68_, selected_track, SC68_DEF_LOOP) < 0) {
        close();
        return {false, "Cannot start default libsc68 track"};
    }

    if (sc68_process(sc68_, nullptr, nullptr) == SC68_ERROR) {
        const char* error = sc68_error(sc68_);
        close();
        return {false, error ? std::string(error) : "Cannot prime libsc68 playback"};
    }

    auto metadata = Sc68MetadataReader::read(source, MetadataReadOptions{.decode_album_art = false});
    if (!metadata) {
        close();
        return {false, metadata.error()};
    }

    const int actual_sample_rate = std::max(sc68_cntl(sc68_, SC68_GET_SPR), 1);

    track_info_ = std::move(*metadata);
    out_fmt_.sample_rate = actual_sample_rate;
    out_fmt_.channels = 2;
    track_info_.sample_rate = actual_sample_rate;
    upsertAnalysisField(track_info_.decoder_analysis, "YM engine", "blep");
    upsertAnalysisField(track_info_.decoder_analysis, "PCM format", "s16");
    upsertAnalysisField(track_info_.decoder_analysis, "Paula interpolation", "disabled");
    upsertAnalysisField(track_info_.decoder_analysis,
                        "Render sample rate",
                        std::format("{} Hz", actual_sample_rate));

    total_frames_ = 0;
    if (track_info_.finite_duration && track_info_.duration_seconds > 0.0) {
        total_frames_ = static_cast<int64_t>(std::llround(
            track_info_.duration_seconds * static_cast<double>(actual_sample_rate)));
    }

    estimated_bitrate_bps_ = track_info_.bitrate_bps;
    replay_gain_scale_ = ReplayGain::linearGainForTrack(track_info_, replay_gain_settings_);
    render_buffer_.assign(kRenderChunkFrames, 0);
    eof_pending_ = false;

    return {true, {}};
}

void Sc68DecoderBackend::close() {
    if (sc68_) {
        sc68_destroy(sc68_);
        sc68_ = nullptr;
    }

    out_fmt_ = {};
    track_info_ = {};
    total_frames_ = 0;
    estimated_bitrate_bps_ = 0;
    replay_gain_scale_ = 1.0f;
    eof_pending_ = false;
    render_buffer_.clear();
}

bool Sc68DecoderBackend::isOpen() const {
    return sc68_ != nullptr;
}

int Sc68DecoderBackend::decodeNextFrames(std::vector<float>& out_pcm) {
    out_pcm.clear();
    if (!sc68_)
        return -1;

    if (eof_pending_)
        return 0;

    if (render_buffer_.size() < static_cast<size_t>(kRenderChunkFrames))
        render_buffer_.assign(kRenderChunkFrames, 0);

    int frames = kRenderChunkFrames;
    const int code = sc68_process(sc68_, render_buffer_.data(), &frames);
    if (code == SC68_ERROR)
        return -1;

    if (frames <= 0) {
        if (code & (SC68_END | SC68_CHANGE)) {
            eof_pending_ = true;
            return 0;
        }
        return -1;
    }

    out_pcm.resize(static_cast<size_t>(frames) * 2);
    for (int index = 0; index < frames; ++index) {
        const uint32_t packed = render_buffer_[static_cast<size_t>(index)];
        const int16_t left = static_cast<int16_t>(packed & 0xFFFFu);
        const int16_t right = static_cast<int16_t>((packed >> 16) & 0xFFFFu);
        out_pcm[static_cast<size_t>(index) * 2] = static_cast<float>(left) * kSc68SampleScale;
        out_pcm[static_cast<size_t>(index) * 2 + 1] = static_cast<float>(right) * kSc68SampleScale;
    }

    if (code & (SC68_END | SC68_CHANGE))
        eof_pending_ = true;

    ReplayGain::apply(std::span<float>(out_pcm), replay_gain_scale_);
    return frames;
}

std::optional<TrackInfo> Sc68DecoderBackend::consumeTrackInfoUpdate() {
    return std::nullopt;
}

bool Sc68DecoderBackend::seek(double /*position_seconds*/) {
    return false;
}

void Sc68DecoderBackend::setReplayGainSettings(ReplayGain::ReplayGainSettings settings) {
    replay_gain_settings_ = settings;
    replay_gain_scale_ = ReplayGain::linearGainForTrack(track_info_, settings);
}

DecoderCapabilities Sc68DecoderBackend::capabilities() const {
    if (!isOpen())
        return {};

    return DecoderCapabilities{
        .can_seek = false,
        .supports_gapless = false,
    };
}

const DecoderProvider& sc68DecoderProvider() {
    static const Sc68DecoderProvider kProvider;
    return kProvider;
}
