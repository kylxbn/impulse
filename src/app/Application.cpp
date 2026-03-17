#include "Application.hpp"
#include "ui/MainWindow.hpp"
#include "core/PlaybackStatusUtils.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

namespace {

constexpr float kMinBufferAheadSeconds = 1.0f;
constexpr float kMaxBufferAheadSeconds = 16.0f;
constexpr float kGaplessCommitLeadSeconds = 0.5f;
constexpr float kGaplessPrepareTargetSeconds = 1.0f;
constexpr int kMaxLiveStreamReconnectAttempts = 3;
constexpr auto kCommandRetryDelay = 1ms;
constexpr auto kLiveStreamReconnectDelay = 500ms;

}  // namespace

// ---------------------------------------------------------------------------
Application::Application() = default;

Application::~Application() {
    // jthread's destructor calls request_stop() + join()
}

void Application::onAudioOutputProgress(void* userdata) noexcept {
    auto* self = static_cast<Application*>(userdata);
    if (!self)
        return;

    self->signalDecodeLoop();
}

void Application::signalDecodeLoop() noexcept {
    decode_wakeup_generation_.fetch_add(1, std::memory_order_release);
    decode_wait_cv_.notify_one();
}

void Application::waitForDecodeLoopSignal(std::stop_token stop,
                                          uint64_t observed_generation,
                                          std::chrono::milliseconds max_wait) {
    auto predicate = [this, observed_generation, stop]() {
        return stop.stop_requested() ||
               decode_wakeup_generation_.load(std::memory_order_acquire) != observed_generation;
    };

    if (predicate())
        return;

    std::unique_lock lock(decode_wait_mutex_);
    if (max_wait == std::chrono::milliseconds::max()) {
        decode_wait_cv_.wait(lock, stop, predicate);
        return;
    }

    decode_wait_cv_.wait_for(lock, stop, max_wait, predicate);
}

// ---------------------------------------------------------------------------
int Application::run() {
    // Start audio output
    if (!audio_output_.init(audio_ring_,
                            bitrate_ring_,
                            playback_state_.current_frame,
                            playback_state_.seek_generation,
                            playback_state_.current_bitrate_bps,
                            playback_state_.volume,
                            &Application::onAudioOutputProgress,
                            this)) {
        return 1;
    }

    // Start decode thread
    decode_thread_ = std::jthread([this](std::stop_token st) {
        decodeLoop(std::move(st));
    });

    // --- SDL3 + ImGui main loop ---
    {
        MainWindow window(*this, browser_);
        while (window.runFrame()) {}
    }

    if (decode_thread_.joinable()) {
        decode_thread_.request_stop();
        signalDecodeLoop();
        decode_thread_.join();
    }
    audio_output_.shutdown();
    return 0;
}

// ---------------------------------------------------------------------------
// Decode thread loop
// ---------------------------------------------------------------------------
void Application::decodeLoop(std::stop_token stop) {
    std::vector<float> pending_pcm;
    size_t pending_samples = 0;
    int64_t pending_bitrate_bps = 0;
    while (!stop.stop_requested()) {
        maybeCommitGaplessTrackSwitches();

        // Drain command queue first
        Command cmd;
        while (command_queue_.try_dequeue(cmd)) {
            if (processCommand(cmd)) {
                pending_pcm.clear();
                pending_samples = 0;
                pending_bitrate_bps = 0;
            }
        }

        maybeCommitGaplessTrackSwitches();
        maybePublishTrackInfoUpdate();
        maybePreparePendingGaplessTrack();
        if (maybeCommitPreparedGaplessTrack(pending_pcm, pending_samples, pending_bitrate_bps))
            maybeCommitGaplessTrackSwitches();

        auto status = playback_state_.status.load(std::memory_order_relaxed);
        const bool buffering = status == PlaybackStatus::Buffering;
        const bool playing = status == PlaybackStatus::Playing;
        const bool live_stream =
            decoder_.is_open() && decoder_.trackInfo().is_stream;
        const uint32_t startup_channels = audio_output_.config().channels;

        if (!playing && !buffering) {
            waitForDecodeLoopSignal(stop,
                                    decode_wakeup_generation_.load(std::memory_order_acquire));
            continue;
        }

        if (buffering) {
            const bool decoder_reached_eof =
                playback_state_.decoder_reached_eof.load(std::memory_order_relaxed);
            const size_t pending_frames = startup_channels > 0 && pending_pcm.size() > pending_samples
                ? (pending_pcm.size() - pending_samples) / startup_channels
                : 0;
            const size_t available_startup_frames = bufferedFrames() + pending_frames;
            if (decoder_reached_eof && available_startup_frames == 0) {
                playback_state_.current_bitrate_bps.store(0, std::memory_order_relaxed);
                playback_state_.status.store(PlaybackStatus::Error, std::memory_order_relaxed);
                publishNowPlaying(nullptr);
                continue;
            }
        } else if (live_stream &&
                   audio_output_.consumeUnderrunDetected() &&
                   !playback_state_.decoder_reached_eof.load(std::memory_order_relaxed)) {
            playback_state_.current_bitrate_bps.store(0, std::memory_order_relaxed);
            live_stream_reconnect_attempts_ = 0;
            playback_state_.status.store(PlaybackStatus::Buffering, std::memory_order_relaxed);
            audio_output_.setPaused(true);
            continue;
        }

        if (pending_samples >= pending_pcm.size()) {
            pending_pcm.clear();
            pending_samples = 0;
            pending_bitrate_bps = 0;
        }

        if (shouldPublishAudibleEndOfTrack(
                playback_state_.decoder_reached_eof.load(std::memory_order_relaxed),
                audio_ring_.available_read(),
                prepared_gapless_track_.has_value(),
                !pending_pcm.empty())) {
            handleEndOfTrack();
            continue;
        }

        const size_t target_ahead_frames =
            target_buffer_ahead_frames_.load(std::memory_order_relaxed);
        if (!buffering &&
            pending_pcm.empty() &&
            audio_ring_.available_read() / audio_output_.config().channels >= target_ahead_frames) {
            waitForDecodeLoopSignal(stop,
                                    decode_wakeup_generation_.load(std::memory_order_acquire));
            continue;
        }

        if (pending_pcm.empty()) {
            if (prepared_gapless_track_ ||
                !decoder_.is_open() ||
                playback_state_.decoder_reached_eof.load(std::memory_order_relaxed)) {
                waitForDecodeLoopSignal(stop,
                                        decode_wakeup_generation_.load(std::memory_order_acquire));
                continue;
            }

            // Decode the next batch only when there is no partially-written
            // audio left to flush into the ring.
            const int result = decoder_.decodeNextFrames(pending_pcm);

            if (result < 0) {
                if (live_stream && reconnectCurrentLiveStream()) {
                    pending_pcm.clear();
                    pending_samples = 0;
                    pending_bitrate_bps = 0;
                    waitForDecodeLoopSignal(stop,
                                            decode_wakeup_generation_.load(std::memory_order_acquire),
                                            kLiveStreamReconnectDelay);
                    continue;
                }
                playback_state_.current_bitrate_bps.store(0, std::memory_order_relaxed);
                playback_state_.status.store(PlaybackStatus::Error,
                                             std::memory_order_relaxed);
                pending_pcm.clear();
                continue;
            }

            if (result == 0) {
                playback_state_.decoder_reached_eof.store(true, std::memory_order_relaxed);
                pending_pcm.clear();
                continue;
            }

            pending_samples = 0;
            pending_bitrate_bps = decoder_.instantaneousBitrateBps();
        }

        const size_t channels = audio_output_.config().channels;
        while (pending_samples < pending_pcm.size() && !stop.stop_requested()) {
            const auto current_status = playback_state_.status.load(std::memory_order_relaxed);
            if (current_status != PlaybackStatus::Playing &&
                current_status != PlaybackStatus::Buffering)
                break;

            if (!bitrate_ring_.canPush()) {
                waitForDecodeLoopSignal(stop,
                                        decode_wakeup_generation_.load(std::memory_order_acquire));
                continue;
            }

            size_t n = audio_ring_.write(pending_pcm.data() + pending_samples,
                                         pending_pcm.size() - pending_samples);
            if (n == 0) {
                waitForDecodeLoopSignal(stop,
                                        decode_wakeup_generation_.load(std::memory_order_acquire));
                continue;
            }

            const uint32_t frames_written = static_cast<uint32_t>(n / channels);
            if (frames_written == 0) {
                waitForDecodeLoopSignal(stop,
                                        decode_wakeup_generation_.load(std::memory_order_acquire));
                continue;
            }

            if (!bitrate_ring_.push(frames_written, pending_bitrate_bps)) {
                waitForDecodeLoopSignal(stop,
                                        decode_wakeup_generation_.load(std::memory_order_acquire));
                continue;
            }

            pending_samples += static_cast<size_t>(frames_written * channels);
        }

        if (playback_state_.status.load(std::memory_order_relaxed) == PlaybackStatus::Buffering) {
            const size_t target_ahead_frames =
                target_buffer_ahead_frames_.load(std::memory_order_relaxed);
            const bool decoder_reached_eof =
                playback_state_.decoder_reached_eof.load(std::memory_order_relaxed);
            const size_t committed_frames = bufferedFrames();
            if (committed_frames >= target_ahead_frames ||
                (decoder_reached_eof && committed_frames > 0)) {
                if (live_stream)
                    live_stream_reconnect_attempts_ = 0;
                playback_state_.status.store(PlaybackStatus::Playing, std::memory_order_relaxed);
                audio_output_.setPaused(false);
                maybeCommitGaplessTrackSwitches();
            }
        }

        if (pending_samples >= pending_pcm.size()) {
            pending_pcm.clear();
            pending_samples = 0;
            pending_bitrate_bps = 0;
        }

        maybeCommitGaplessTrackSwitches();
    }
}

// ---------------------------------------------------------------------------
void Application::enqueueCommand(const Command& cmd) {
    while (!command_queue_.try_enqueue(cmd)) {
        if (!decode_thread_.joinable())
            return;
        std::this_thread::sleep_for(kCommandRetryDelay);
    }
    signalDecodeLoop();
}

void Application::discardBufferedAudio(int64_t target_frame) {
    const int next_generation =
        playback_state_.seek_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
    audio_output_.waitForSeekGeneration(next_generation);

    audio_ring_.reset();
    bitrate_ring_.reset();
    clearPendingGaplessState();
    clearGaplessTrackSwitches();
    playback_state_.decoder_reached_eof.store(false, std::memory_order_relaxed);
    playback_state_.current_bitrate_bps.store(0, std::memory_order_relaxed);
    playback_state_.current_frame.store(target_frame, std::memory_order_relaxed);
}

void Application::maybePublishTrackInfoUpdate() {
    const auto updated_info = decoder_.consumeTrackInfoUpdate();
    if (!updated_info)
        return;

    auto now_playing = currentNowPlaying();
    if (!now_playing)
        return;

    auto refreshed = std::make_shared<NowPlayingTrack>(*now_playing);
    refreshed->track_info = std::make_shared<TrackInfo>(*updated_info);
    refreshed->total_frames = decoder_.totalFrames();
    publishNowPlaying(std::move(refreshed));
}

bool Application::reconnectCurrentLiveStream() {
    const auto now_playing = currentNowPlaying();
    const MediaSource source = decoder_.trackInfo().source.empty() && now_playing && now_playing->track_info
        ? now_playing->track_info->source
        : decoder_.trackInfo().source;
    if (source.empty() || !source.isUrl())
        return false;

    if (live_stream_reconnect_attempts_ >= kMaxLiveStreamReconnectAttempts)
        return false;
    ++live_stream_reconnect_attempts_;

    const uint64_t playlist_tab_id = now_playing ? now_playing->playlist_tab_id : 0;
    const uint64_t playlist_item_id = now_playing ? now_playing->playlist_item_id : 0;

    audio_output_.setPaused(true);
    playback_state_.current_bitrate_bps.store(0, std::memory_order_relaxed);
    playback_state_.decoder_reached_eof.store(false, std::memory_order_relaxed);
    playback_state_.status.store(PlaybackStatus::Buffering, std::memory_order_relaxed);
    discardBufferedAudio(0);
    decoder_.close();

    auto result = decoder_.open(source, audio_output_.config().sample_rate);
    if (!result.ok)
        return false;

    auto refreshed = std::make_shared<NowPlayingTrack>();
    refreshed->track_info = std::make_shared<TrackInfo>(decoder_.trackInfo());
    refreshed->total_frames = decoder_.totalFrames();
    refreshed->start_frame = 0;
    refreshed->playlist_tab_id = playlist_tab_id;
    refreshed->playlist_item_id = playlist_item_id;
    publishNowPlaying(std::move(refreshed));
    return true;
}

// ---------------------------------------------------------------------------
bool Application::processCommand(const Command& cmd) {
    return std::visit([this](const auto& c) {
        using T = std::decay_t<decltype(c)>;

        if constexpr (std::is_same_v<T, CommandOpenFile>) {
            openTrack(c.source, false, false, c.playlist_tab_id, c.playlist_item_id);
            return true;

        } else if constexpr (std::is_same_v<T, CommandOpenFileGapless>) {
            pending_gapless_request_.schedule(
                GaplessPendingRequest{
                    c.source,
                    c.playlist_tab_id,
                    c.playlist_item_id,
                    c.playlist_revision,
                    c.policy_generation,
                },
                currentPlaylistRevision(c.playlist_tab_id),
                currentGaplessPolicyGeneration());
            return false;

        } else if constexpr (std::is_same_v<T, CommandPlay>) {
            return false;

        } else if constexpr (std::is_same_v<T, CommandPause>) {
            return false;

        } else if constexpr (std::is_same_v<T, CommandStop>) {
            decoder_.close();
            discardBufferedAudio(0);
            audio_output_.setPaused(false);
            publishNowPlaying(nullptr);
            playback_state_.status.store(PlaybackStatus::Stopped,
                                         std::memory_order_relaxed);
            return true;

        } else if constexpr (std::is_same_v<T, CommandSeek>) {
            if (!decoder_.is_open()) return false;
            // TODO: Cancel any staged gapless preload and then honor the seek.
            if (prepared_gapless_track_) return false;
            playback_state_.seek_in_progress.store(true, std::memory_order_relaxed);
            const auto previous_status = playback_state_.status.load(std::memory_order_relaxed);
            playback_state_.status.store(PlaybackStatus::Seeking, std::memory_order_relaxed);
            if (!decoder_.seek(c.position_seconds)) {
                playback_state_.status.store(previous_status, std::memory_order_relaxed);
                playback_state_.seek_in_progress.store(false, std::memory_order_relaxed);
                return false;
            }
            int64_t target_frame = static_cast<int64_t>(
                c.position_seconds * audio_output_.config().sample_rate);
            discardBufferedAudio(target_frame);
            if (auto now_playing = currentNowPlaying()) {
                auto sought_track = std::make_shared<NowPlayingTrack>(*now_playing);
                sought_track->start_frame = 0;
                publishNowPlaying(std::move(sought_track));
            }
            playback_state_.status.store(resumeStatusAfterSuccessfulSeek(previous_status),
                                         std::memory_order_relaxed);
            playback_state_.seek_in_progress.store(false, std::memory_order_relaxed);
            return true;

        } else if constexpr (std::is_same_v<T, CommandSetVolume>) {
            audio_output_.setVolume(c.linear_gain);
            return false;

        } else if constexpr (std::is_same_v<T, CommandSetReplayGainSettings>) {
            decoder_.setReplayGainSettings(c.settings);
            return false;

        } else if constexpr (std::is_same_v<T, CommandQuit>) {
            decode_thread_.request_stop();
            return false;
        }
        // CommandNext / CommandPrev are handled by the playlist manager
        // and translated into CommandOpenFile by the UI, handled there.
        return false;
    }, cmd);
}

// ---------------------------------------------------------------------------
bool Application::openTrack(const MediaSource& source,
                            bool preserve_buffered_audio,
                            bool defer_public_switch_until_boundary,
                            uint64_t playlist_tab_id,
                            uint64_t playlist_item_id) {
    clearPendingGaplessState();
    decoder_.close();
    playback_state_.decoder_reached_eof.store(false, std::memory_order_relaxed);

    if (!preserve_buffered_audio)
        discardBufferedAudio(0);

    auto result = decoder_.open(source, audio_output_.config().sample_rate);
    if (!result.ok) {
        playback_state_.current_bitrate_bps.store(0, std::memory_order_relaxed);
        if (!preserve_buffered_audio) {
            publishNowPlaying(nullptr);
            playback_state_.status.store(PlaybackStatus::Error,
                                         std::memory_order_relaxed);
        }
        return false;
    }

    auto now_playing = std::make_shared<NowPlayingTrack>();
    now_playing->track_info = std::make_shared<TrackInfo>(decoder_.trackInfo());
    now_playing->total_frames = decoder_.totalFrames();
    now_playing->playlist_tab_id = playlist_tab_id;
    now_playing->playlist_item_id = playlist_item_id;

    if (defer_public_switch_until_boundary) {
        const uint32_t channels = audio_output_.config().channels;
        const int64_t absolute_frame = playback_state_.current_frame.load(std::memory_order_relaxed);
        const int64_t buffered_frames = channels > 0
            ? static_cast<int64_t>(audio_ring_.available_read() / channels)
            : 0;

        now_playing->start_frame = absolute_frame + buffered_frames;
        gapless_track_switches_.enqueue(std::move(now_playing));
    } else {
        now_playing->start_frame = 0;
        publishNowPlaying(std::move(now_playing));
    }

    playback_state_.current_bitrate_bps.store(0, std::memory_order_relaxed);
    const bool buffering_stream = decoder_.trackInfo().is_stream;
    if (buffering_stream)
        live_stream_reconnect_attempts_ = 0;
    audio_output_.setPaused(buffering_stream);
    playback_state_.status.store(buffering_stream ? PlaybackStatus::Buffering
                                                  : PlaybackStatus::Playing,
                                 std::memory_order_relaxed);
    return true;
}

// ---------------------------------------------------------------------------
void Application::handleEndOfTrack() {
    playback_state_.current_bitrate_bps.store(0, std::memory_order_relaxed);
    const bool has_pending_track =
        prepared_gapless_track_.has_value() || pending_gapless_request_.peek() != nullptr;
    playback_state_.status.store(has_pending_track ? PlaybackStatus::EndOfTrack
                                                   : PlaybackStatus::Stopped,
                                 std::memory_order_relaxed);
    // TODO: playlist manager will post CommandOpenFile for next track
}

void Application::maybePreparePendingGaplessTrack() {
    const GaplessPendingRequest* scheduled_request = nullptr;
    if (const GaplessPendingRequest* pending_request = pending_gapless_request_.peek()) {
        scheduled_request = pending_gapless_request_.current(
            currentPlaylistRevision(pending_request->playlist_tab_id),
            currentGaplessPolicyGeneration());
    }
    if (!scheduled_request) {
        clearPreparedGaplessTrack();
        return;
    }
    const GaplessPendingRequest* pending_request = scheduled_request;

    const bool prepared_matches_pending = prepared_gapless_track_ &&
        prepared_gapless_track_->request.playlist_tab_id == pending_request->playlist_tab_id &&
        prepared_gapless_track_->request.playlist_item_id == pending_request->playlist_item_id &&
        prepared_gapless_track_->request.playlist_revision == pending_request->playlist_revision &&
        prepared_gapless_track_->request.policy_generation == pending_request->policy_generation &&
        prepared_gapless_track_->request.source == pending_request->source;

    if (!prepared_matches_pending)
        clearPreparedGaplessTrack();

    if (!prepared_gapless_track_) {
        decoder_.close();

        auto result = decoder_.open(pending_request->source, audio_output_.config().sample_rate);
        if (!result.ok) {
            pending_gapless_request_.clear();
            playback_state_.current_bitrate_bps.store(0, std::memory_order_relaxed);
            return;
        }

        auto now_playing = std::make_shared<NowPlayingTrack>();
        now_playing->track_info = std::make_shared<TrackInfo>(decoder_.trackInfo());
        now_playing->total_frames = decoder_.totalFrames();
        now_playing->playlist_tab_id = pending_request->playlist_tab_id;
        now_playing->playlist_item_id = pending_request->playlist_item_id;

        prepared_gapless_track_.emplace(PreparedGaplessTrack{
            .request = *pending_request,
            .now_playing = std::move(now_playing),
            .staged_pcm = {},
            .staged_bitrate_bps = decoder_.trackInfo().bitrate_bps,
            .reached_eof = false,
        });
    }

    const uint32_t channels = audio_output_.config().channels;
    const size_t target_samples = static_cast<size_t>(gaplessPrepareTargetFrames()) * channels;
    std::vector<float> decoded_chunk;
    while (prepared_gapless_track_ &&
           !prepared_gapless_track_->reached_eof &&
           prepared_gapless_track_->staged_pcm.size() < target_samples) {
        const int result = decoder_.decodeNextFrames(decoded_chunk);
        if (result < 0) {
            clearPreparedGaplessTrack();
            pending_gapless_request_.clear();
            playback_state_.current_bitrate_bps.store(0, std::memory_order_relaxed);
            return;
        }

        if (result == 0) {
            prepared_gapless_track_->reached_eof = true;
            break;
        }

        prepared_gapless_track_->staged_bitrate_bps = decoder_.instantaneousBitrateBps();
        prepared_gapless_track_->staged_pcm.insert(prepared_gapless_track_->staged_pcm.end(),
                                                   decoded_chunk.begin(),
                                                   decoded_chunk.end());
    }
}

bool Application::maybeCommitPreparedGaplessTrack(std::vector<float>& pending_pcm,
                                                  size_t& pending_samples,
                                                  int64_t& pending_bitrate_bps) {
    const auto status = playback_state_.status.load(std::memory_order_relaxed);
    if (status != PlaybackStatus::EndOfTrack && status != PlaybackStatus::Playing)
        return false;
    if (!prepared_gapless_track_)
        return false;
    if (prepared_gapless_track_->request.policy_generation != currentGaplessPolicyGeneration()) {
        pending_gapless_request_.clear();
        clearPreparedGaplessTrack();
        return false;
    }
    if (!pending_pcm.empty())
        return false;
    if (prepared_gapless_track_->staged_pcm.empty() &&
        !prepared_gapless_track_->reached_eof) {
        return false;
    }

    const uint32_t buffered_frames = bufferedFrames();
    if (buffered_frames > gaplessCommitLeadFrames())
        return false;

    // This is the gapless point of no return: once we move the prepared PCM
    // into the write path and queue the public switch, later UI policy changes
    // (repeat mode, etc.) cannot reliably cancel the transition without
    // risking an audible gap.
    auto prepared = std::move(*prepared_gapless_track_);
    prepared_gapless_track_.reset();
    pending_gapless_request_.clear();

    prepared.now_playing->start_frame =
        playback_state_.current_frame.load(std::memory_order_relaxed) + buffered_frames;
    gapless_track_switches_.enqueue(std::move(prepared.now_playing));

    pending_pcm = std::move(prepared.staged_pcm);
    pending_samples = 0;
    pending_bitrate_bps = prepared.staged_bitrate_bps;

    playback_state_.decoder_reached_eof.store(false, std::memory_order_relaxed);
    playback_state_.current_bitrate_bps.store(0, std::memory_order_relaxed);
    audio_output_.setPaused(false);
    playback_state_.status.store(PlaybackStatus::Playing, std::memory_order_relaxed);
    return true;
}

void Application::maybeCommitGaplessTrackSwitches() {
    const int64_t absolute_frame =
        playback_state_.current_frame.load(std::memory_order_relaxed);

    while (auto next = gapless_track_switches_.popReady(absolute_frame)) {
        publishNowPlaying(std::move(next));
    }
}

void Application::clearPendingGaplessState() {
    pending_gapless_request_.clear();
    clearPreparedGaplessTrack();
}

void Application::clearPreparedGaplessTrack() {
    if (!prepared_gapless_track_)
        return;

    decoder_.close();
    prepared_gapless_track_.reset();
}

void Application::clearGaplessTrackSwitches() {
    gapless_track_switches_.clear();
}

void Application::publishNowPlaying(std::shared_ptr<NowPlayingTrack> now_playing) {
    now_playing_.store(std::move(now_playing));
}

uint32_t Application::clampBufferAheadFrames(float seconds) const {
    const float clamped_seconds = std::clamp(seconds,
                                             kMinBufferAheadSeconds,
                                             kMaxBufferAheadSeconds);
    const uint32_t sample_rate = audio_output_.config().sample_rate;
    const uint32_t channels = audio_output_.config().channels;
    const uint32_t max_frames = channels > 0
        ? static_cast<uint32_t>(AudioOutput::kRingCapacity / channels)
        : 0;
    const uint32_t requested_frames = static_cast<uint32_t>(clamped_seconds * sample_rate);

    if (max_frames == 0)
        return requested_frames;

    return std::clamp<uint32_t>(requested_frames, 1u, max_frames);
}

uint32_t Application::bufferedFrames() const {
    const uint32_t channels = audio_output_.config().channels;
    if (channels == 0)
        return 0;

    return static_cast<uint32_t>(audio_ring_.available_read() / channels);
}

uint32_t Application::gaplessCommitLeadFrames() const {
    const uint32_t sample_rate = audio_output_.config().sample_rate;
    const uint32_t callback_warmup_frames =
        std::max(1u, audio_output_.config().buffer_frames * 2u);
    const uint32_t time_warmup_frames =
        static_cast<uint32_t>(sample_rate * kGaplessCommitLeadSeconds);
    return std::max(callback_warmup_frames, time_warmup_frames);
}

uint32_t Application::gaplessPrepareTargetFrames() const {
    const uint32_t sample_rate = audio_output_.config().sample_rate;
    const uint32_t time_target_frames =
        static_cast<uint32_t>(sample_rate * kGaplessPrepareTargetSeconds);
    return std::max(gaplessCommitLeadFrames(), time_target_frames);
}

// ---------------------------------------------------------------------------
// UI accessors
// ---------------------------------------------------------------------------

void Application::commandOpenFile(MediaSource source,
                                  uint64_t playlist_tab_id,
                                  uint64_t playlist_item_id) {
    enqueueCommand(CommandOpenFile{std::move(source), playlist_tab_id, playlist_item_id});
}
void Application::commandOpenFile(std::filesystem::path path,
                                  uint64_t playlist_tab_id,
                                  uint64_t playlist_item_id) {
    commandOpenFile(MediaSource::fromPath(std::move(path)), playlist_tab_id, playlist_item_id);
}
void Application::commandOpenFileGapless(MediaSource source,
                                         uint64_t playlist_tab_id,
                                         uint64_t playlist_item_id,
                                         uint64_t playlist_revision) {
    enqueueCommand(CommandOpenFileGapless{
        std::move(source),
        playlist_tab_id,
        playlist_item_id,
        playlist_revision,
        currentGaplessPolicyGeneration(),
    });
}
void Application::commandOpenFileGapless(std::filesystem::path path,
                                         uint64_t playlist_tab_id,
                                         uint64_t playlist_item_id,
                                         uint64_t playlist_revision) {
    commandOpenFileGapless(MediaSource::fromPath(std::move(path)),
                           playlist_tab_id,
                           playlist_item_id,
                           playlist_revision);
}
void Application::invalidatePendingGaplessRequests() {
    gapless_policy_generation_.fetch_add(1, std::memory_order_acq_rel);
    signalDecodeLoop();
}
void Application::notifyPlaylistChanged(uint64_t playlist_tab_id, uint64_t playlist_revision) {
    std::scoped_lock lock(playlist_revisions_mutex_);
    playlist_revisions_[playlist_tab_id] = playlist_revision;
    signalDecodeLoop();
}
void Application::forgetPlaylist(uint64_t playlist_tab_id) {
    std::scoped_lock lock(playlist_revisions_mutex_);
    playlist_revisions_.erase(playlist_tab_id);
    signalDecodeLoop();
}
void Application::commandPlay() {
    if (playback_state_.status.load(std::memory_order_relaxed) != PlaybackStatus::Paused)
        return;

    playback_state_.current_bitrate_bps.store(0, std::memory_order_relaxed);
    const bool live_stream = decoder_.is_open() && decoder_.trackInfo().is_stream;
    if (live_stream) {
        const size_t target_ahead_frames =
            target_buffer_ahead_frames_.load(std::memory_order_relaxed);
        const bool decoder_reached_eof =
            playback_state_.decoder_reached_eof.load(std::memory_order_relaxed);
        const size_t committed_frames = bufferedFrames();
        if (committed_frames < target_ahead_frames &&
            !(decoder_reached_eof && committed_frames > 0)) {
            playback_state_.status.store(PlaybackStatus::Buffering, std::memory_order_relaxed);
            audio_output_.setPaused(true);
            signalDecodeLoop();
            return;
        }
        live_stream_reconnect_attempts_ = 0;
    }
    playback_state_.status.store(PlaybackStatus::Playing, std::memory_order_relaxed);
    audio_output_.setPaused(false);
    signalDecodeLoop();
}
void Application::commandPause() {
    const auto status = playback_state_.status.load(std::memory_order_relaxed);
    if (status != PlaybackStatus::Playing &&
        status != PlaybackStatus::EndOfTrack &&
        status != PlaybackStatus::Buffering)
        return;

    playback_state_.current_bitrate_bps.store(0, std::memory_order_relaxed);
    playback_state_.status.store(PlaybackStatus::Paused, std::memory_order_relaxed);
    audio_output_.setPaused(true);
    signalDecodeLoop();
}
void Application::commandSetBufferAheadSeconds(float seconds) {
    target_buffer_ahead_frames_.store(clampBufferAheadFrames(seconds),
                                      std::memory_order_relaxed);
    signalDecodeLoop();
}
void Application::commandStop()                   { enqueueCommand(CommandStop{}); }
void Application::commandSeek(double s)           { enqueueCommand(CommandSeek{s}); }
void Application::commandSetVolume(float g)       { audio_output_.setVolume(g); }
void Application::commandSetReplayGainSettings(ReplayGain::ReplayGainSettings settings) {
    enqueueCommand(CommandSetReplayGainSettings{settings});
}
void Application::commandNext()                   { enqueueCommand(CommandNext{}); }
void Application::commandPrev()                   { enqueueCommand(CommandPrev{}); }
void Application::commandQuit()                   { enqueueCommand(CommandQuit{}); }

uint64_t Application::currentPlaylistRevision(uint64_t playlist_tab_id) const {
    std::scoped_lock lock(playlist_revisions_mutex_);
    if (auto it = playlist_revisions_.find(playlist_tab_id); it != playlist_revisions_.end())
        return it->second;
    return 0;
}

uint64_t Application::currentGaplessPolicyGeneration() const {
    return gapless_policy_generation_.load(std::memory_order_acquire);
}

PlaybackStatus Application::playbackStatus() const {
    return playback_state_.status.load(std::memory_order_relaxed);
}

bool Application::decoderReachedEof() const {
    return playback_state_.decoder_reached_eof.load(std::memory_order_relaxed);
}

double Application::positionSeconds() const {
    const auto now_playing = currentNowPlaying();
    const int64_t absolute_frames =
        playback_state_.current_frame.load(std::memory_order_relaxed);
    const int64_t track_start_frame = now_playing ? now_playing->start_frame : 0;
    const int64_t frames = std::max<int64_t>(0, absolute_frames - track_start_frame);
    uint32_t rate  = audio_output_.config().sample_rate;
    return rate > 0 ? static_cast<double>(frames) / rate : 0.0;
}

double Application::durationSeconds() const {
    const auto now_playing = currentNowPlaying();
    const int64_t frames = now_playing ? now_playing->total_frames : 0;
    uint32_t rate  = audio_output_.config().sample_rate;
    return rate > 0 ? static_cast<double>(frames) / rate : 0.0;
}

float Application::volume() const {
    return playback_state_.volume.load(std::memory_order_relaxed);
}

int64_t Application::instantaneousBitrateBps() const {
    return playback_state_.current_bitrate_bps.load(std::memory_order_relaxed);
}

size_t Application::bufferedSamples() const {
    return audio_ring_.available_read();
}

size_t Application::ringReadPosition() const {
    return audio_ring_.read_position();
}

size_t Application::ringWritePosition() const {
    return audio_ring_.write_position();
}

size_t Application::ringCapacitySamples() const {
    return AudioOutput::kRingCapacity;
}

uint32_t Application::outputSampleRate() const {
    return audio_output_.config().sample_rate;
}

uint32_t Application::outputChannels() const {
    return audio_output_.config().channels;
}

std::shared_ptr<NowPlayingTrack> Application::currentNowPlaying() const {
    return now_playing_.load();
}
