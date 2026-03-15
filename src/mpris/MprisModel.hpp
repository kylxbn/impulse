#pragma once

#include "app/AppSettings.hpp"
#include "core/NowPlayingTrack.hpp"
#include "core/PlaybackState.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

enum class MprisLoopStatus {
    None,
    Track,
    Playlist,
};

enum class MprisCommandType {
    Raise,
    Quit,
    Play,
    Pause,
    PlayPause,
    Stop,
    Next,
    Previous,
    SeekRelative,
    SetPosition,
    SetVolume,
    SetLoopStatus,
    OpenUri,
};

struct MprisTrackMetadata {
    std::string              track_id;
    std::string              url;
    std::string              art_url;
    std::string              title;
    std::vector<std::string> artists;
    std::string              album;
    std::vector<std::string> genres;
    int64_t                  length_us = 0;
    int32_t                  track_number = 0;
    bool                     has_track_number = false;

    bool operator==(const MprisTrackMetadata& other) const = default;
};

struct MprisSnapshot {
    PlaybackStatus  playback_status = PlaybackStatus::Stopped;
    MprisLoopStatus loop_status = MprisLoopStatus::None;
    bool            shuffle = false;
    bool            can_go_next = false;
    bool            can_go_previous = false;
    bool            can_play = false;
    bool            can_pause = false;
    bool            can_seek = false;
    bool            can_control = true;
    double          rate = 1.0;
    double          minimum_rate = 1.0;
    double          maximum_rate = 1.0;
    double          volume = 1.0;
    int64_t         position_us = 0;
    MprisTrackMetadata track;

    bool operator==(const MprisSnapshot& other) const = default;
};

struct MprisCommand {
    MprisCommandType type = MprisCommandType::Play;
    int64_t          position_us = 0;
    double           volume = 1.0;
    MprisLoopStatus  loop_status = MprisLoopStatus::None;
    std::string      track_id;
    std::string      uri;

    bool operator==(const MprisCommand& other) const = default;
};

constexpr std::string_view kMprisObjectPath = "/org/mpris/MediaPlayer2";
constexpr std::string_view kMprisRootInterface = "org.mpris.MediaPlayer2";
constexpr std::string_view kMprisPlayerInterface = "org.mpris.MediaPlayer2.Player";
constexpr std::string_view kMprisDesktopEntry = "impulse";
constexpr std::string_view kMprisIdentity = "Impulse";
constexpr std::string_view kMprisBaseServiceName = "org.mpris.MediaPlayer2.impulse";

[[nodiscard]] const char* playbackStatusToMprisString(PlaybackStatus status);
[[nodiscard]] MprisLoopStatus repeatModeToMprisLoopStatus(RepeatMode mode);
[[nodiscard]] RepeatMode mprisLoopStatusToRepeatMode(MprisLoopStatus status);
[[nodiscard]] const char* mprisLoopStatusToString(MprisLoopStatus status);
[[nodiscard]] std::optional<MprisLoopStatus> parseMprisLoopStatus(std::string_view value);
[[nodiscard]] std::string buildMprisTrackId(const NowPlayingTrack& track);
[[nodiscard]] MprisTrackMetadata buildMprisTrackMetadata(const NowPlayingTrack& track,
                                                         uint32_t sample_rate);
[[nodiscard]] std::string filePathToUri(const std::filesystem::path& path);
[[nodiscard]] std::optional<std::filesystem::path> fileUriToPath(std::string_view uri);
[[nodiscard]] bool isSupportedOpenUri(const std::filesystem::path& path);
[[nodiscard]] std::optional<MediaSource> mediaSourceFromOpenUri(std::string_view uri);
[[nodiscard]] const std::vector<std::string>& supportedMimeTypes();
[[nodiscard]] const std::vector<std::string>& supportedUriSchemes();
[[nodiscard]] int64_t secondsToMprisTime(double seconds);
[[nodiscard]] double mprisTimeToSeconds(int64_t usec);
