#pragma once

#include "audio/AudioOutput.hpp"
#include "audio/BitrateSpanQueue.hpp"
#include "audio/Decoder.hpp"
#include "audio/RingBuffer.hpp"
#include "app/GaplessPendingRequest.hpp"
#include "app/GaplessTrackQueue.hpp"
#include "browser/FileBrowser.hpp"
#include "core/Events.hpp"
#include "core/NowPlayingTrack.hpp"
#include "core/PlaybackState.hpp"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>

#include <readerwriterqueue.h>

class Application {
public:
    Application();
    ~Application();

    int run();

    // --- UI thread API (call only from main/UI thread) ---
    void commandOpenFile(MediaSource source,
                         uint64_t playlist_tab_id = 0,
                         uint64_t playlist_item_id = 0);
    void commandOpenFile(std::filesystem::path path,
                         uint64_t playlist_tab_id = 0,
                         uint64_t playlist_item_id = 0);
    void commandOpenFileGapless(MediaSource source,
                                uint64_t playlist_tab_id = 0,
                                uint64_t playlist_item_id = 0,
                                uint64_t playlist_revision = 0);
    void commandOpenFileGapless(std::filesystem::path path,
                                uint64_t playlist_tab_id = 0,
                                uint64_t playlist_item_id = 0,
                                uint64_t playlist_revision = 0);
    void invalidatePendingGaplessRequests();
    void notifyPlaylistChanged(uint64_t playlist_tab_id, uint64_t playlist_revision);
    void forgetPlaylist(uint64_t playlist_tab_id);
    void commandPlay();
    void commandPause();
    void commandStop();
    void commandSeek(double seconds);
    void commandSetVolume(float linear_gain);
    void commandSetBufferAheadSeconds(float seconds);
    void commandSetReplayGainSettings(ReplayGain::ReplayGainSettings settings);
    void commandNext();
    void commandPrev();
    void commandQuit();

    // Read-only accessors for the UI (atomics, safe from any thread)
    PlaybackStatus       playbackStatus()  const;
    bool                 decoderReachedEof() const;
    double               positionSeconds() const;
    double               durationSeconds() const;
    float                volume()          const;
    int64_t              instantaneousBitrateBps() const;
    float                currentPeakAbs()  const;
    bool                 clippedDetected() const;
    void                 clearClippedIndicator();
    size_t               bufferedSamples() const;
    size_t               ringReadPosition() const;
    size_t               ringWritePosition() const;
    size_t               ringCapacitySamples() const;
    uint32_t             outputSampleRate() const;
    uint32_t             outputChannels() const;

    std::shared_ptr<NowPlayingTrack> currentNowPlaying() const;

private:
    static void onAudioOutputProgress(void* userdata) noexcept;
    void decodeLoop(std::stop_token stop);
    void enqueueCommand(const Command& cmd);
    void signalDecodeLoop() noexcept;
    void waitForDecodeLoopSignal(std::stop_token stop,
                                 uint64_t observed_generation,
                                 std::chrono::milliseconds max_wait = std::chrono::milliseconds::max());
    void discardBufferedAudio(int64_t target_frame);
    bool processCommand(const Command& cmd);
    bool openTrack(const MediaSource& source,
                   bool preserve_buffered_audio,
                   bool defer_public_switch_until_boundary,
                   uint64_t playlist_tab_id,
                   uint64_t playlist_item_id);
    bool reconnectCurrentLiveStream();
    void handleEndOfTrack();
    void maybePreparePendingGaplessTrack();
    bool maybeCommitPreparedGaplessTrack(std::vector<float>& pending_pcm,
                                         size_t& pending_samples,
                                         int64_t& pending_bitrate_bps);
    void clearPendingGaplessState();
    void clearPreparedGaplessTrack();
    void maybeCommitGaplessTrackSwitches();
    void maybePublishTrackInfoUpdate();
    void clearGaplessTrackSwitches();
    void publishNowPlaying(std::shared_ptr<NowPlayingTrack> now_playing);
    void resetCurrentPlaybackTelemetry() noexcept;
    uint64_t currentPlaylistRevision(uint64_t playlist_tab_id) const;
    uint64_t currentGaplessPolicyGeneration() const;
    uint32_t clampBufferAheadFrames(float seconds) const;
    uint32_t bufferedFrames() const;
    uint32_t gaplessCommitLeadFrames() const;
    uint32_t gaplessPrepareTargetFrames() const;

    struct PreparedGaplessTrack {
        GaplessPendingRequest              request;
        std::shared_ptr<NowPlayingTrack>  now_playing;
        std::vector<float>                staged_pcm;
        int64_t                           staged_bitrate_bps = 0;
        bool                              reached_eof = false;
    };

    // --- Shared state (atomics, safe across threads) ---
    PlaybackState playback_state_;

    std::atomic<std::shared_ptr<NowPlayingTrack>> now_playing_;

    // --- Audio pipeline ---
    AudioOutput::Ring audio_ring_;
    AudioOutput::BitrateRing bitrate_ring_;
    AudioOutput       audio_output_;
    Decoder           decoder_;

    // --- UI-owned (main thread only) ---
    FileBrowser browser_;

    // --- Threading ---
    moodycamel::ReaderWriterQueue<Command> command_queue_{64};
    std::jthread                           decode_thread_;
    mutable std::mutex                     decode_wait_mutex_;
    std::condition_variable_any            decode_wait_cv_;
    std::atomic<uint64_t>                  decode_wakeup_generation_{0};
    GaplessPendingRequestState             pending_gapless_request_;
    std::optional<PreparedGaplessTrack>    prepared_gapless_track_;
    GaplessTrackQueue                      gapless_track_switches_;
    std::atomic<uint64_t>                  gapless_policy_generation_{1};
    std::atomic<uint32_t>                  target_buffer_ahead_frames_{16u * 48000u};
    mutable std::mutex                     playlist_revisions_mutex_;
    std::unordered_map<uint64_t, uint64_t> playlist_revisions_;
    int                                    live_stream_reconnect_attempts_ = 0;
};
