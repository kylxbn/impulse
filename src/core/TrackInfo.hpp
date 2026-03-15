#pragma once

#include "MediaSource.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct TrackInfoField {
    std::string label;
    std::string value;
};

struct TrackInfo {
    MediaSource           source;

    // File
    std::filesystem::path path;
    int64_t               file_size_bytes  = 0;
    double                duration_seconds = 0.0;
    bool                  is_stream = false;
    bool                  seekable = true;
    bool                  finite_duration = true;

    // Codec / technical
    std::string codec_name;        // e.g. "flac", "mp3", "aac", "opus"
    std::string container_format;  // e.g. "ogg", "matroska", "mp3"
    int64_t     bitrate_bps    = 0;
    int         sample_rate    = 0;
    int         bit_depth      = 0;  // 0 if lossy / unknown
    int         channels       = 0;
    std::string channel_layout;    // e.g. "stereo", "5.1(side)"
    int         initial_padding_samples = 0;
    int         trailing_padding_samples = 0;
    int         seek_preroll_samples = 0;
    bool        manual_skip_export_enabled = false;
    std::vector<TrackInfoField> ffmpeg_analysis;
    std::vector<TrackInfoField> stream_metadata;
    std::vector<TrackInfoField> format_metadata;

    // Tags
    std::string title;
    std::string artist;
    std::string album;
    std::string year;
    std::string genre;
    std::string track_number;
    std::string comment;

    // ReplayGain (from tags, never computed on the fly)
    std::optional<float> rg_track_gain_db;
    std::optional<float> rg_album_gain_db;
    std::optional<float> rg_track_peak;   // linear, 0.0-1.0+
    std::optional<float> rg_album_peak;

    // Album art (decoded to RGBA; upload as SDL_Texture on main thread)
    std::vector<uint8_t> album_art_rgba;
    int                  album_art_width  = 0;
    int                  album_art_height = 0;
    std::filesystem::path external_album_art_path;
};
