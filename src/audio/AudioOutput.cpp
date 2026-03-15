#include "AudioOutput.hpp"

#include <pipewire/pipewire.h>
#include <pipewire/thread-loop.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <spa/utils/result.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace {

constexpr uint32_t kMaxVolumeChannels = 32;

float clampLinearGain(float linear_gain) {
    if (!std::isfinite(linear_gain))
        return 1.0f;

    return std::clamp(linear_gain, 0.0f, 1.0f);
}

float volumeFromValues(const float* values, uint32_t count) {
    if (!values || count == 0)
        return 1.0f;

    float volume = 0.0f;
    for (uint32_t i = 0; i < count; ++i)
        volume = std::max(volume, values[i]);
    return clampLinearGain(volume);
}

}  // namespace

// ---------------------------------------------------------------------------
// We store a pw_stream_events struct on the heap so we can set up the
// on_process callback pointer without a global.  The 'version' field must
// be set to PW_VERSION_STREAM_EVENTS.
// ---------------------------------------------------------------------------

AudioOutput::AudioOutput() {
    events_ = new pw_stream_events{};
    events_->version = PW_VERSION_STREAM_EVENTS;
    events_->control_info = &AudioOutput::onControlInfo;
    events_->process = &AudioOutput::onProcess;
}

AudioOutput::~AudioOutput() {
    shutdown();
    delete events_;
}

// ---------------------------------------------------------------------------
bool AudioOutput::init(Ring&                 ring,
                       BitrateRing&          bitrate_ring,
                       std::atomic<int64_t>& frame_counter,
                       std::atomic<int>&     seek_generation,
                       std::atomic<int64_t>& current_bitrate_bps,
                       std::atomic<float>&   stream_volume) {
    ring_                 = &ring;
    bitrate_ring_         = &bitrate_ring;
    frame_ctr_            = &frame_counter;
    seek_gen_             = &seek_generation;
    current_bitrate_bps_  = &current_bitrate_bps;
    observed_volume_      = &stream_volume;
    last_seek_gen_        = seek_generation.load(std::memory_order_acquire);
    observed_seek_gen_.store(last_seek_gen_, std::memory_order_release);

    pw_init(nullptr, nullptr);

    loop_ = pw_thread_loop_new("impulse-pw", nullptr);
    if (!loop_) return false;

    // Create a simple audio playback stream
    pw_properties* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE,     "Audio",
        PW_KEY_MEDIA_CATEGORY, "Playback",
        PW_KEY_MEDIA_ROLE,     "Music",
        PW_KEY_APP_NAME,       "impulse",
        nullptr);

    stream_ = pw_stream_new_simple(
        pw_thread_loop_get_loop(loop_),
        "impulse-playback",
        props,
        events_,
        this);  // userdata

    if (!stream_) {
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
        return false;
    }

    // Build audio format parameter
    uint8_t buffer[1024];
    spa_pod_builder b{};
    spa_pod_builder_init(&b, buffer, sizeof(buffer));

    spa_audio_info_raw info{};
    info.format   = SPA_AUDIO_FORMAT_F32;
    info.rate     = config_.sample_rate;
    info.channels = config_.channels;
    info.position[0] = SPA_AUDIO_CHANNEL_FL;
    info.position[1] = SPA_AUDIO_CHANNEL_FR;

    const spa_pod* params[1];
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

    const int connect_result = pw_stream_connect(
        stream_,
        PW_DIRECTION_OUTPUT,
        PW_ID_ANY,
        static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT |
            PW_STREAM_FLAG_MAP_BUFFERS |
            PW_STREAM_FLAG_RT_PROCESS),
        params, 1);
    if (connect_result < 0) {
        pw_stream_destroy(stream_);
        stream_ = nullptr;
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
        return false;
    }

    const int start_result = pw_thread_loop_start(loop_);
    if (start_result < 0) {
        pw_stream_destroy(stream_);
        stream_ = nullptr;
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
        return false;
    }

    stream_active_.store(true, std::memory_order_release);
    return true;
}

// ---------------------------------------------------------------------------
void AudioOutput::shutdown() {
    stream_active_.store(false, std::memory_order_release);
    if (loop_) pw_thread_loop_stop(loop_);
    if (stream_) { pw_stream_destroy(stream_); stream_ = nullptr; }
    if (loop_)   { pw_thread_loop_destroy(loop_); loop_ = nullptr; }
    observed_volume_ = nullptr;
    pw_deinit();
}

void AudioOutput::setVolume(float linear_gain) {
    const float clamped_gain = clampLinearGain(linear_gain);
    if (observed_volume_)
        observed_volume_->store(clamped_gain, std::memory_order_relaxed);

    if (!loop_ || !stream_) {
        pending_volume_ = clamped_gain;
        has_pending_volume_ = true;
        return;
    }

    pw_thread_loop_lock(loop_);
    pending_volume_ = clamped_gain;
    has_pending_volume_ = true;
    applyPendingVolumeLocked();
    pw_thread_loop_unlock(loop_);
}

float AudioOutput::volume() const {
    if (!observed_volume_)
        return 1.0f;

    return observed_volume_->load(std::memory_order_relaxed);
}

void AudioOutput::waitForSeekGeneration(int generation,
                                        std::chrono::milliseconds poll_interval) {
    while (stream_active_.load(std::memory_order_acquire) &&
           observed_seek_gen_.load(std::memory_order_acquire) != generation) {
        std::this_thread::sleep_for(poll_interval);
    }
}

void AudioOutput::onControlInfo(void* userdata,
                                uint32_t id,
                                const pw_stream_control* control) {
    auto* self = static_cast<AudioOutput*>(userdata);

    if (id == SPA_PROP_channelVolumes) {
        self->has_channel_volume_control_ = control && control->max_values > 0;
        if (self->has_channel_volume_control_) {
            const uint32_t advertised_count = control->n_values > 0
                ? control->n_values
                : control->max_values;
            self->channel_volume_count_ = std::clamp<uint32_t>(advertised_count, 1u, kMaxVolumeChannels);
        } else {
            self->channel_volume_count_ = 0;
        }
    } else if (id == SPA_PROP_volume) {
        self->has_scalar_volume_control_ = control && control->max_values > 0;
    } else {
        return;
    }

    self->updateObservedVolume(id, control);
    self->applyPendingVolumeLocked();
}

void AudioOutput::applyPendingVolumeLocked() {
    if (!has_pending_volume_)
        return;

    if (applyVolumeLocked(pending_volume_))
        has_pending_volume_ = false;
}

bool AudioOutput::applyVolumeLocked(float linear_gain) {
    if (!stream_)
        return false;

    const float clamped_gain = clampLinearGain(linear_gain);

    if (has_channel_volume_control_ && channel_volume_count_ > 0) {
        std::array<float, kMaxVolumeChannels> channel_volumes{};
        channel_volumes.fill(clamped_gain);
        if (pw_stream_set_control(stream_,
                                  SPA_PROP_channelVolumes,
                                  channel_volume_count_,
                                  channel_volumes.data()) == 0) {
            return true;
        }
    }

    if (has_scalar_volume_control_) {
        float scalar_volume = clamped_gain;
        if (pw_stream_set_control(stream_, SPA_PROP_volume, 1, &scalar_volume) == 0)
            return true;
    }

    return false;
}

void AudioOutput::updateObservedVolume(uint32_t id, const pw_stream_control* control) {
    if (!observed_volume_ || !control)
        return;

    if (id == SPA_PROP_channelVolumes) {
        observed_volume_->store(volumeFromValues(control->values, control->n_values),
                                std::memory_order_relaxed);
        return;
    }

    if (id == SPA_PROP_volume && !has_channel_volume_control_) {
        observed_volume_->store(volumeFromValues(control->values, control->n_values),
                                std::memory_order_relaxed);
    }
}

// ---------------------------------------------------------------------------
// PipeWire realtime callback, runs on PW's managed thread.
// CONSTRAINTS: no malloc, no mutex, no blocking, no printf.
// ---------------------------------------------------------------------------
void AudioOutput::onProcess(void* userdata) {
    auto* self = static_cast<AudioOutput*>(userdata);

    pw_buffer* pwbuf = pw_stream_dequeue_buffer(self->stream_);
    if (!pwbuf) return;

    spa_buffer* spabuf = pwbuf->buffer;
    auto* dst = static_cast<float*>(spabuf->datas[0].data);
    const uint32_t max_frames = spabuf->datas[0].maxsize
                              / sizeof(float)
                              / self->config_.channels;
    uint32_t n_frames = static_cast<uint32_t>(pwbuf->requested);
    if (n_frames == 0)
        n_frames = std::min(self->config_.buffer_frames, max_frames);
    else
        n_frames = std::min(n_frames, max_frames);

    // Check for seek, fill silence for one cycle to let ring buffer refill
    int gen = self->seek_gen_->load(std::memory_order_acquire);
    if (gen != self->last_seek_gen_) {
        self->last_seek_gen_ = gen;
        self->observed_seek_gen_.store(gen, std::memory_order_release);
        self->current_bitrate_bps_->store(0, std::memory_order_relaxed);
        self->underrun_detected_.store(false, std::memory_order_relaxed);
        std::memset(dst, 0, n_frames * self->config_.channels * sizeof(float));
        spabuf->datas[0].chunk->offset = 0;
        spabuf->datas[0].chunk->stride = static_cast<int32_t>(
            self->config_.channels * sizeof(float));
        spabuf->datas[0].chunk->size   = n_frames * self->config_.channels * sizeof(float);
        pw_stream_queue_buffer(self->stream_, pwbuf);
        return;
    }

    if (self->paused_.load(std::memory_order_relaxed)) {
        self->current_bitrate_bps_->store(0, std::memory_order_relaxed);
        self->underrun_detected_.store(false, std::memory_order_relaxed);
        std::memset(dst, 0, n_frames * self->config_.channels * sizeof(float));
        spabuf->datas[0].chunk->offset = 0;
        spabuf->datas[0].chunk->stride = static_cast<int32_t>(
            self->config_.channels * sizeof(float));
        spabuf->datas[0].chunk->size   = n_frames * self->config_.channels * sizeof(float);
        pwbuf->size = n_frames;
        pw_stream_queue_buffer(self->stream_, pwbuf);
        return;
    }

    uint32_t n_samples = n_frames * self->config_.channels;
    size_t   got       = self->ring_->read(dst, n_samples);

    // Zero-fill any underrun
    if (got < n_samples) {
        self->underrun_detected_.store(true, std::memory_order_relaxed);
        std::memset(dst + got, 0, (n_samples - got) * sizeof(float));
    }

    const uint32_t got_frames = static_cast<uint32_t>(got / self->config_.channels);
    const int64_t current_bitrate = got_frames > 0
        ? self->bitrate_ring_->consumeFrames(got_frames)
        : 0;
    self->current_bitrate_bps_->store(current_bitrate, std::memory_order_relaxed);

    // Advance frame counter (frames, not samples)
    self->frame_ctr_->fetch_add(
        static_cast<int64_t>(got_frames),
        std::memory_order_relaxed);

    spabuf->datas[0].chunk->offset = 0;
    spabuf->datas[0].chunk->stride = static_cast<int32_t>(
        self->config_.channels * sizeof(float));
    spabuf->datas[0].chunk->size   = n_samples * sizeof(float);
    pwbuf->size = n_frames;

    pw_stream_queue_buffer(self->stream_, pwbuf);
}
