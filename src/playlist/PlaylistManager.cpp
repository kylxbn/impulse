#include "PlaylistManager.hpp"

#include "audio/ReplayGain.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <compare>
#include <random>
#include <ranges>
#include <utility>

namespace {

std::string toLower(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::strong_ordering compareStrings(const std::string& lhs, const std::string& rhs) {
    return toLower(lhs) <=> toLower(rhs);
}

std::optional<int> parseTrackNumber(const std::string& value) {
    if (value.empty()) return std::nullopt;

    const char* begin = value.data();
    const char* end = begin;
    while (end != begin + value.size() && std::isdigit(static_cast<unsigned char>(*end)))
        ++end;
    if (end == begin) return std::nullopt;

    int parsed = 0;
    auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end) return std::nullopt;
    return parsed;
}

template <typename T>
bool compareMaybeDescending(const T& lhs, const T& rhs, PlaylistSortDirection direction) {
    if (direction == PlaylistSortDirection::Ascending)
        return lhs < rhs;
    return rhs < lhs;
}

}  // namespace

std::string PlaylistItem::displayTitle() const {
    if (!title.empty()) return title;
    return file_name;
}

PlaylistItem PlaylistManager::makeItem(uint64_t id,
                                       MediaSource source,
                                       const TrackInfo* info) const {
    PlaylistItem item;
    item.id        = id;
    item.source    = std::move(source);
    item.path      = item.source.path;
    item.file_name = item.source.displayName();
    if (info) {
        item.title            = info->title;
        item.artist           = info->artist;
        item.album            = info->album;
        item.track_number     = info->track_number;
        item.codec            = info->codec_name;
        item.duration_seconds = info->duration_seconds;
        item.bitrate_bps      = info->bitrate_bps;
        item.track_replay_gain_db = info->rg_track_gain_db;
        item.album_replay_gain_db = info->rg_album_gain_db;
        item.track_replay_gain_peak = info->rg_track_peak;
        item.album_replay_gain_peak = info->rg_album_peak;
        recomputePlr(item);
    }
    return item;
}

void PlaylistManager::recomputePlr(PlaylistItem& item) const {
    item.plr_lu = ReplayGain::plrLu(item.track_replay_gain_db,
                                    item.track_replay_gain_peak,
                                    plr_reference_lufs_);
}

void PlaylistManager::rebindCurrentIndex(uint64_t current_id_hint) {
    if (tracks_.empty()) {
        current_idx_ = 0;
        return;
    }

    auto it = std::ranges::find(tracks_, current_id_hint, &PlaylistItem::id);
    if (it != tracks_.end()) {
        current_idx_ = static_cast<size_t>(std::distance(tracks_.begin(), it));
        return;
    }

    if (current_idx_ >= tracks_.size())
        current_idx_ = tracks_.size() - 1;
}

void PlaylistManager::addTrack(MediaSource source, const TrackInfo* info) {
    tracks_.push_back(makeItem(next_id_++, std::move(source), info));
}

void PlaylistManager::addTrack(std::filesystem::path path, const TrackInfo* info) {
    addTrack(MediaSource::fromPath(std::move(path)), info);
}

void PlaylistManager::addTracks(const std::vector<MediaSource>& sources) {
    for (const auto& source : sources)
        addTrack(source);
}

void PlaylistManager::addTracks(const std::vector<std::filesystem::path>& paths) {
    for (const auto& path : paths)
        addTrack(path);
}

void PlaylistManager::insertNext(MediaSource source, const TrackInfo* info) {
    if (tracks_.empty()) {
        addTrack(std::move(source), info);
        return;
    }

    const size_t insert_at = std::min(current_idx_ + 1, tracks_.size());
    tracks_.insert(tracks_.begin() + static_cast<ptrdiff_t>(insert_at),
                   makeItem(next_id_++, std::move(source), info));

    if (insert_at <= current_idx_)
        ++current_idx_;
}

void PlaylistManager::insertNext(std::filesystem::path path, const TrackInfo* info) {
    insertNext(MediaSource::fromPath(std::move(path)), info);
}

void PlaylistManager::insertNext(const std::vector<MediaSource>& sources) {
    if (sources.empty()) return;
    if (tracks_.empty()) {
        addTracks(sources);
        return;
    }

    size_t insert_at = std::min(current_idx_ + 1, tracks_.size());
    std::vector<PlaylistItem> items;
    items.reserve(sources.size());
    for (const auto& source : sources)
        items.push_back(makeItem(next_id_++, source, nullptr));

    tracks_.insert(tracks_.begin() + static_cast<ptrdiff_t>(insert_at),
                   std::make_move_iterator(items.begin()),
                   std::make_move_iterator(items.end()));

    if (insert_at <= current_idx_)
        current_idx_ += items.size();
}

void PlaylistManager::insertNext(const std::vector<std::filesystem::path>& paths) {
    if (paths.empty()) return;
    if (tracks_.empty()) {
        addTracks(paths);
        return;
    }

    size_t insert_at = std::min(current_idx_ + 1, tracks_.size());
    std::vector<PlaylistItem> items;
    items.reserve(paths.size());
    for (const auto& path : paths)
        items.push_back(makeItem(next_id_++, MediaSource::fromPath(path), nullptr));

    tracks_.insert(tracks_.begin() + static_cast<ptrdiff_t>(insert_at),
                   std::make_move_iterator(items.begin()),
                   std::make_move_iterator(items.end()));

    if (insert_at <= current_idx_)
        current_idx_ += items.size();
}

void PlaylistManager::removeTrack(size_t index) {
    removeTracks({index});
}

void PlaylistManager::removeTracks(const std::vector<size_t>& indices) {
    if (indices.empty() || tracks_.empty()) return;

    const uint64_t current_id = hasCurrentTrack() ? tracks_[current_idx_].id : 0;
    std::vector<size_t> sorted = indices;
    std::ranges::sort(sorted);
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
        if (*it >= tracks_.size()) continue;
        tracks_.erase(tracks_.begin() + static_cast<ptrdiff_t>(*it));
    }

    rebindCurrentIndex(current_id);
}

void PlaylistManager::clear() {
    tracks_.clear();
    current_idx_ = 0;
}

void PlaylistManager::moveTracks(const std::vector<size_t>& indices, size_t destination_index) {
    if (indices.empty() || tracks_.empty()) return;

    std::vector<size_t> sorted = indices;
    std::ranges::sort(sorted);
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    sorted.erase(std::remove_if(sorted.begin(), sorted.end(), [&](size_t index) {
        return index >= tracks_.size();
    }), sorted.end());
    if (sorted.empty()) return;

    const uint64_t current_id = hasCurrentTrack() ? tracks_[current_idx_].id : 0;

    std::vector<PlaylistItem> moving;
    moving.reserve(sorted.size());
    for (size_t index : sorted)
        moving.push_back(tracks_[index]);

    size_t adjusted_destination = std::min(destination_index, tracks_.size());
    for (size_t index : sorted) {
        if (index < adjusted_destination)
            --adjusted_destination;
    }

    for (auto it = sorted.rbegin(); it != sorted.rend(); ++it)
        tracks_.erase(tracks_.begin() + static_cast<ptrdiff_t>(*it));

    tracks_.insert(tracks_.begin() + static_cast<ptrdiff_t>(adjusted_destination),
                   std::make_move_iterator(moving.begin()),
                   std::make_move_iterator(moving.end()));

    rebindCurrentIndex(current_id);
}

void PlaylistManager::sortBy(PlaylistSortKey key, PlaylistSortDirection direction) {
    if (tracks_.size() < 2 || key == PlaylistSortKey::Order) return;

    const uint64_t current_id = hasCurrentTrack() ? tracks_[current_idx_].id : 0;

    auto compare = [&](const PlaylistItem& lhs, const PlaylistItem& rhs) {
        auto compareStringsForDirection = [&](const std::string& a, const std::string& b) {
            if (compareStrings(a, b) == std::strong_ordering::less)
                return direction == PlaylistSortDirection::Ascending;
            if (compareStrings(a, b) == std::strong_ordering::greater)
                return direction == PlaylistSortDirection::Descending;
            return compareStrings(lhs.file_name, rhs.file_name) == std::strong_ordering::less;
        };

        switch (key) {
            case PlaylistSortKey::TrackNumber: {
                const auto lhs_track = parseTrackNumber(lhs.track_number).value_or(0);
                const auto rhs_track = parseTrackNumber(rhs.track_number).value_or(0);
                return compareMaybeDescending(lhs_track, rhs_track, direction);
            }
            case PlaylistSortKey::Album:
                return compareStringsForDirection(lhs.album, rhs.album);
            case PlaylistSortKey::Title:
                return compareStringsForDirection(lhs.displayTitle(), rhs.displayTitle());
            case PlaylistSortKey::Artist:
                return compareStringsForDirection(lhs.artist, rhs.artist);
            case PlaylistSortKey::Duration:
                return compareMaybeDescending(lhs.duration_seconds, rhs.duration_seconds, direction);
            case PlaylistSortKey::Codec:
                return compareStringsForDirection(lhs.codec, rhs.codec);
            case PlaylistSortKey::Bitrate:
                return compareMaybeDescending(lhs.bitrate_bps, rhs.bitrate_bps, direction);
            case PlaylistSortKey::ReplayGain:
                return compareMaybeDescending(lhs.track_replay_gain_db.value_or(0.0f),
                                              rhs.track_replay_gain_db.value_or(0.0f),
                                              direction);
            case PlaylistSortKey::PLR:
                return compareMaybeDescending(lhs.plr_lu.value_or(0.0f),
                                              rhs.plr_lu.value_or(0.0f),
                                              direction);
            case PlaylistSortKey::FileName:
                return compareStringsForDirection(lhs.file_name, rhs.file_name);
            case PlaylistSortKey::Path:
                return compareStringsForDirection(lhs.path.string(), rhs.path.string());
            case PlaylistSortKey::Order:
                return lhs.id < rhs.id;
        }
        return lhs.id < rhs.id;
    };

    std::stable_sort(tracks_.begin(), tracks_.end(), compare);
    rebindCurrentIndex(current_id);
}

template <typename URBG>
void PlaylistManager::shuffleWithEngine(URBG& rng) {
    if (tracks_.size() < 2)
        return;

    const uint64_t current_id = hasCurrentTrack() ? tracks_[current_idx_].id : 0;
    std::shuffle(tracks_.begin(), tracks_.end(), rng);
    rebindCurrentIndex(current_id);
}

void PlaylistManager::shuffle() {
    std::random_device rd;
    std::mt19937_64 rng(
        (static_cast<uint64_t>(rd()) << 32u) ^ static_cast<uint64_t>(rd()));
    shuffleWithEngine(rng);
}

void PlaylistManager::shuffle(uint64_t seed) {
    std::mt19937_64 rng(seed);
    shuffleWithEngine(rng);
}

void PlaylistManager::setPlrReferenceLufs(float reference_lufs) {
    plr_reference_lufs_ = reference_lufs;
    for (auto& item : tracks_)
        recomputePlr(item);
}

bool PlaylistManager::setCurrentIndex(size_t index) {
    if (index >= tracks_.size()) return false;
    bool changed = (index != current_idx_);
    current_idx_ = index;
    return changed;
}

std::optional<MediaSource> PlaylistManager::currentTrack() const {
    if (!hasCurrentTrack()) return std::nullopt;
    return tracks_[current_idx_].source;
}

std::optional<MediaSource> PlaylistManager::nextTrack() {
    if (tracks_.empty()) return std::nullopt;
    if (current_idx_ + 1 >= tracks_.size()) return std::nullopt;
    ++current_idx_;
    return tracks_[current_idx_].source;
}

std::optional<MediaSource> PlaylistManager::prevTrack() {
    if (tracks_.empty() || current_idx_ == 0) return std::nullopt;
    --current_idx_;
    return tracks_[current_idx_].source;
}

std::optional<MediaSource> PlaylistManager::peekNext() const {
    if (current_idx_ + 1 >= tracks_.size()) return std::nullopt;
    return tracks_[current_idx_ + 1].source;
}

std::optional<MediaSource> PlaylistManager::peekPrev() const {
    if (tracks_.empty() || current_idx_ == 0) return std::nullopt;
    return tracks_[current_idx_ - 1].source;
}

std::optional<size_t> PlaylistManager::indexOf(uint64_t id) const {
    auto it = std::ranges::find(tracks_, id, &PlaylistItem::id);
    if (it == tracks_.end()) return std::nullopt;
    return static_cast<size_t>(std::distance(tracks_.begin(), it));
}
