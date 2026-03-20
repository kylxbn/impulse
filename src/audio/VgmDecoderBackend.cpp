#include "VgmDecoderBackend.hpp"

#include "audio/ReplayGain.hpp"
#include "core/SupportedFormats.hpp"
#include "metadata/VgmMetadataReader.hpp"

#include "emu/Resampler.h"
#include "player/playerbase.hpp"
#include "player/vgmplayer.hpp"
#include "utils/DataLoader.h"
#include "utils/FileLoader.h"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <span>

namespace {

// libvgm's internal mix buffer uses roughly 24-bit sample scale packed into
// INT32. Match PlayerA::SampleConv_toF32 upstream.
constexpr double kInt32ToFloatScale = 1.0 / 8388608.0;

UINT8 onVgmPlayerEvent(PlayerBase* /*player*/,
                       void* /*user_param*/,
                       UINT8 event_type,
                       void* /*event_param*/) {
    return event_type == PLREVT_LOOP ? 0x01 : 0x00;
}

}  // namespace

namespace {

class VgmDecoderProvider final : public DecoderProvider {
public:
    [[nodiscard]] std::string_view name() const override {
        return "libvgm";
    }

    [[nodiscard]] bool supportsSource(const MediaSource& source) const override {
        return source.isFile() && isVgmExtension(source.extension());
    }

    [[nodiscard]] DecoderCapabilities capabilitiesForSource(const MediaSource& /*source*/) const override {
        return DecoderCapabilities{
            .can_seek = true,
            .supports_gapless = true,
        };
    }

    [[nodiscard]] std::unique_ptr<DecoderBackend> createBackend() const override {
        return std::make_unique<VgmDecoderBackend>();
    }

    std::expected<TrackInfo, std::string>
    readMetadata(const MediaSource& source,
                 MetadataReadOptions options) const override {
        return VgmMetadataReader::read(source, options);
    }
};

}  // namespace

VgmDecoderBackend::VgmDecoderBackend() = default;

VgmDecoderBackend::~VgmDecoderBackend() {
    close();
}

namespace {

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

}  // namespace

DecoderBackend::OpenResult VgmDecoderBackend::open(const MediaSource& source,
                                                   int output_sample_rate) {
    close();

    loader_ = FileLoader_Init(source.path.c_str());
    if (!loader_)
        return {false, "Cannot initialize libvgm file loader"};

    DataLoader_SetPreloadBytes(loader_, 0x100);
    const UINT8 load_result = DataLoader_Load(loader_);
    if (load_result != 0) {
        close();
        return {false, "Cannot load VGM data"};
    }

    player_ = std::make_unique<VGMPlayer>();
    if (player_->SetSampleRate(static_cast<UINT32>(std::max(output_sample_rate, 1))) != 0) {
        close();
        return {false, "Cannot configure libvgm sample rate"};
    }
    player_->SetEventCallback(&onVgmPlayerEvent, nullptr);
    if (player_->LoadFile(loader_) != 0) {
        close();
        return {false, "Cannot open VGM file"};
    }
    if (player_->Start() != 0) {
        close();
        return {false, "Cannot start VGM playback"};
    }

    auto metadata = VgmMetadataReader::read(source, MetadataReadOptions{.decode_album_art = false});
    if (!metadata) {
        close();
        return {false, metadata.error()};
    }

    track_info_ = std::move(*metadata);
    out_fmt_.sample_rate = output_sample_rate;
    out_fmt_.channels = 2;
    track_info_.sample_rate = output_sample_rate;
    upsertAnalysisField(track_info_.decoder_analysis,
                        "Render sample rate",
                        std::format("{} Hz", output_sample_rate));
    estimated_bitrate_bps_ = track_info_.bitrate_bps;
    replay_gain_scale_ = ReplayGain::linearGainForTrack(track_info_, replay_gain_settings_);
    render_buffer_.clear();
    render_buffer_.resize(kRenderChunkFrames);

    total_frames_ = static_cast<int64_t>(player_->Tick2Sample(player_->GetTotalTicks()));

    return {true, {}};
}

void VgmDecoderBackend::close() {
    if (player_) {
        player_->Stop();
        player_->UnloadFile();
    }
    player_.reset();

    if (loader_) {
        DataLoader_Deinit(loader_);
        loader_ = nullptr;
    }

    out_fmt_ = {};
    track_info_ = {};
    total_frames_ = 0;
    estimated_bitrate_bps_ = 0;
    replay_gain_scale_ = 1.0f;
    render_buffer_.clear();
}

bool VgmDecoderBackend::isOpen() const {
    return player_ != nullptr && loader_ != nullptr;
}

int VgmDecoderBackend::decodeNextFrames(std::vector<float>& out_pcm) {
    out_pcm.clear();
    if (!player_)
        return -1;

    if (player_->GetState() & PLAYSTATE_END)
        return 0;

    if (render_buffer_.size() < kRenderChunkFrames)
        render_buffer_.resize(kRenderChunkFrames);

    // libvgm mixes device output into the caller-provided buffer. Match the
    // upstream PlayerA/vgmtest callers and clear it before every render.
    std::fill(render_buffer_.begin(), render_buffer_.end(), WAVE_32BS{});

    const UINT32 playback_position_before = player_->GetCurPos(PLAYPOS_SAMPLE);
    const UINT32 rendered = player_->Render(static_cast<UINT32>(kRenderChunkFrames),
                                            render_buffer_.data());
    if (rendered == 0)
        return (player_->GetState() & PLAYSTATE_END) ? 0 : -1;

    const UINT32 playback_position_after = player_->GetCurPos(PLAYPOS_SAMPLE);
    if (playback_position_after <= playback_position_before &&
        !(player_->GetState() & PLAYSTATE_END)) {
        out_pcm.clear();
        return -1;
    }

    out_pcm.reserve(static_cast<size_t>(rendered) * 2);
    for (UINT32 index = 0; index < rendered; ++index) {
        out_pcm.push_back(static_cast<float>(
            std::clamp(render_buffer_[index].L * kInt32ToFloatScale, -1.0, 1.0)));
        out_pcm.push_back(static_cast<float>(
            std::clamp(render_buffer_[index].R * kInt32ToFloatScale, -1.0, 1.0)));
    }

    ReplayGain::apply(std::span<float>(out_pcm), replay_gain_scale_);
    return static_cast<int>(rendered);
}

std::optional<TrackInfo> VgmDecoderBackend::consumeTrackInfoUpdate() {
    return std::nullopt;
}

bool VgmDecoderBackend::seek(double position_seconds) {
    if (!player_ || !track_info_.seekable || out_fmt_.sample_rate <= 0)
        return false;

    const double clamped_seconds = std::clamp(position_seconds,
                                              0.0,
                                              track_info_.duration_seconds > 0.0
                                                  ? track_info_.duration_seconds
                                                  : position_seconds);
    const auto target_sample = static_cast<UINT32>(std::llround(
        clamped_seconds * static_cast<double>(out_fmt_.sample_rate)));
    return player_->Seek(PLAYPOS_SAMPLE, target_sample) == 0;
}

void VgmDecoderBackend::setReplayGainSettings(ReplayGain::ReplayGainSettings settings) {
    replay_gain_settings_ = settings;
    replay_gain_scale_ = ReplayGain::linearGainForTrack(track_info_, settings);
}

const DecoderProvider& vgmDecoderProvider() {
    static const VgmDecoderProvider kProvider;
    return kProvider;
}
