#pragma once

#include "core/MediaSource.hpp"
#include "core/TrackInfo.hpp"

#include <filesystem>
#include <random>
#include <optional>
#include <string>
#include <vector>

enum class PlaylistSortKey {
    Order,
    TrackNumber,
    Album,
    Title,
    Artist,
    Duration,
    Codec,
    Bitrate,
    ReplayGain,
    PLR,
    FileName,
    Path
};

enum class PlaylistSortDirection {
    Ascending,
    Descending
};

struct PlaylistItem {
    uint64_t              id = 0;
    MediaSource           source;
    std::filesystem::path path;
    std::string           file_name;
    std::string           title;
    std::string           artist;
    std::string           album;
    std::string           track_number;
    std::string           codec;
    double                duration_seconds = 0.0;
    int64_t               bitrate_bps = 0;
    std::optional<float>  track_replay_gain_db;
    std::optional<float>  album_replay_gain_db;
    std::optional<float>  track_replay_gain_peak;
    std::optional<float>  album_replay_gain_peak;
    std::optional<float>  plr_lu;

    [[nodiscard]] std::string displayTitle() const;
};

// Single flat playlist. UI-thread-only, no internal synchronisation.
class PlaylistManager {
public:
    void addTrack(MediaSource source, const TrackInfo* info = nullptr);
    void addTrack(std::filesystem::path path, const TrackInfo* info = nullptr);
    void addTracks(const std::vector<MediaSource>& sources);
    void addTracks(const std::vector<std::filesystem::path>& paths);
    void insertNext(MediaSource source, const TrackInfo* info = nullptr);
    void insertNext(std::filesystem::path path, const TrackInfo* info = nullptr);
    void insertNext(const std::vector<MediaSource>& sources);
    void insertNext(const std::vector<std::filesystem::path>& paths);
    void removeTrack(size_t index);
    void removeTracks(const std::vector<size_t>& indices);
    void clear();

    void moveTracks(const std::vector<size_t>& indices, size_t destination_index);
    void sortBy(PlaylistSortKey key, PlaylistSortDirection direction);
    void shuffle();
    void shuffle(uint64_t seed);
    void setPlrReferenceLufs(float reference_lufs);
    float plrReferenceLufs() const { return plr_reference_lufs_; }

    // Returns true if the index changed (i.e. track is different)
    bool setCurrentIndex(size_t index);

    std::optional<MediaSource> currentTrack() const;
    std::optional<MediaSource> nextTrack();   // advances index
    std::optional<MediaSource> prevTrack();   // decrements index
    std::optional<MediaSource> peekNext() const;
    std::optional<MediaSource> peekPrev() const;

    bool  empty()        const { return tracks_.empty(); }
    size_t size()        const { return tracks_.size(); }
    size_t currentIndex() const { return current_idx_; }
    bool hasCurrentTrack() const { return !tracks_.empty() && current_idx_ < tracks_.size(); }

    const std::vector<PlaylistItem>& tracks() const { return tracks_; }
    std::optional<size_t> indexOf(uint64_t id) const;

private:
    template <typename URBG>
    void shuffleWithEngine(URBG& rng);

    PlaylistItem makeItem(uint64_t id, MediaSource source, const TrackInfo* info) const;
    void recomputePlr(PlaylistItem& item) const;
    void rebindCurrentIndex(uint64_t current_id_hint);

    std::vector<PlaylistItem> tracks_;
    size_t current_idx_ = 0;
    uint64_t next_id_ = 1;
    float plr_reference_lufs_ = -18.0f;
};
