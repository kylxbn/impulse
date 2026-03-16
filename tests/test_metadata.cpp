#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "metadata/MetadataReader.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

const std::filesystem::path kTempRoot =
    std::filesystem::temp_directory_path() / "impulse_test_metadata_reader";

std::vector<uint8_t> decodeBase64(std::string_view input) {
    auto decodeChar = [](char ch) -> int {
        if (ch >= 'A' && ch <= 'Z') return ch - 'A';
        if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
        if (ch >= '0' && ch <= '9') return ch - '0' + 52;
        if (ch == '+') return 62;
        if (ch == '/') return 63;
        return -1;
    };

    std::vector<uint8_t> output;
    int val = 0;
    int valb = -8;

    for (char ch : input) {
        if (ch == '=')
            break;
        const int decoded = decodeChar(ch);
        if (decoded < 0)
            continue;
        val = (val << 6) + decoded;
        valb += 6;
        if (valb >= 0) {
            output.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }

    return output;
}

void writeBinaryFile(const std::filesystem::path& path,
                     const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out.good());
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    REQUIRE(out.good());
}

void appendLe16(std::vector<uint8_t>& bytes, uint16_t value) {
    bytes.push_back(static_cast<uint8_t>(value & 0xFFu));
    bytes.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
}

void appendLe32(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(static_cast<uint8_t>(value & 0xFFu));
    bytes.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
    bytes.push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
    bytes.push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
}

std::filesystem::path createTestWaveFile(const std::filesystem::path& directory,
                                         std::string_view               filename) {
    constexpr uint16_t kChannels = 2;
    constexpr uint32_t kSampleRate = 44100;
    constexpr uint16_t kBitsPerSample = 16;
    constexpr uint16_t kBytesPerSample = kBitsPerSample / 8;
    constexpr uint32_t kFrameCount = kSampleRate / 4;
    constexpr uint32_t kDataSize = kFrameCount * kChannels * kBytesPerSample;
    constexpr double kFrequencyHz = 440.0;
    constexpr double kAmplitude = 0.35;

    std::vector<uint8_t> wav_bytes;
    wav_bytes.reserve(44 + kDataSize);

    wav_bytes.insert(wav_bytes.end(), {'R', 'I', 'F', 'F'});
    appendLe32(wav_bytes, 36 + kDataSize);
    wav_bytes.insert(wav_bytes.end(), {'W', 'A', 'V', 'E'});
    wav_bytes.insert(wav_bytes.end(), {'f', 'm', 't', ' '});
    appendLe32(wav_bytes, 16);
    appendLe16(wav_bytes, 1);
    appendLe16(wav_bytes, kChannels);
    appendLe32(wav_bytes, kSampleRate);
    appendLe32(wav_bytes, kSampleRate * kChannels * kBytesPerSample);
    appendLe16(wav_bytes, kChannels * kBytesPerSample);
    appendLe16(wav_bytes, kBitsPerSample);
    wav_bytes.insert(wav_bytes.end(), {'d', 'a', 't', 'a'});
    appendLe32(wav_bytes, kDataSize);

    for (uint32_t frame = 0; frame < kFrameCount; ++frame) {
        const double angle = 2.0 * std::numbers::pi * kFrequencyHz * frame / kSampleRate;
        const int16_t sample = static_cast<int16_t>(32767.0 * kAmplitude * std::sin(angle));
        for (uint16_t channel = 0; channel < kChannels; ++channel)
            appendLe16(wav_bytes, static_cast<uint16_t>(sample));
    }

    const auto path = directory / std::string(filename);
    writeBinaryFile(path, wav_bytes);
    return path;
}

std::filesystem::path prepareTempRoot(std::string_view suite_name) {
    std::error_code cleanup_ec;
    std::filesystem::remove_all(kTempRoot, cleanup_ec);

    const auto path = kTempRoot / std::string(suite_name);
    REQUIRE(std::filesystem::create_directories(path));
    return path;
}

std::optional<std::string> ffmpegAnalysisValue(const TrackInfo& info,
                                               std::string_view label) {
    for (const auto& field : info.ffmpeg_analysis) {
        if (field.label == label)
            return field.value;
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("MetadataReader - read returns expected fields") {
    const auto temp_dir = prepareTempRoot("read_fields");
    const auto test_file = createTestWaveFile(temp_dir, "fixture.wav");

    auto result = MetadataReader::read(test_file);
    REQUIRE(result.has_value());

    const TrackInfo& info = result.value();

    CHECK(info.path == test_file);
    CHECK(info.file_size_bytes > 0);
    CHECK(info.duration_seconds > 0.0);
    CHECK(!info.codec_name.empty());
    CHECK(!info.container_format.empty());
    CHECK(info.sample_rate > 0);
    CHECK(info.channels > 0);
    CHECK(info.bit_depth >= 0);
    CHECK(!info.ffmpeg_analysis.empty());
    CHECK(info.initial_padding_samples >= 0);
    CHECK(info.trailing_padding_samples >= 0);
    CHECK(info.seek_preroll_samples >= 0);
    CHECK(info.album_art_rgba.empty());
    CHECK(info.external_album_art_path.empty());

    // Print a summary for manual inspection
    MESSAGE("  codec:     " << info.codec_name);
    MESSAGE("  container: " << info.container_format);
    MESSAGE("  sample_rate: " << info.sample_rate);
    MESSAGE("  channels:    " << info.channels);
    MESSAGE("  bit_depth:   " << info.bit_depth);
    MESSAGE("  bitrate:     " << info.bitrate_bps);
    MESSAGE("  duration:    " << info.duration_seconds << "s");
    MESSAGE("  layout:      " << info.channel_layout);
    MESSAGE("  title:   " << info.title);
    MESSAGE("  artist:  " << info.artist);
    MESSAGE("  album:   " << info.album);
    MESSAGE("  year:    " << info.year);
    MESSAGE("  genre:   " << info.genre);
    MESSAGE("  track#:  " << info.track_number);

    if (info.rg_track_gain_db)
        MESSAGE("  RG track gain: " << *info.rg_track_gain_db << " dB");
    if (info.rg_track_peak)
        MESSAGE("  RG track peak: " << *info.rg_track_peak);
    if (info.rg_album_gain_db)
        MESSAGE("  RG album gain: " << *info.rg_album_gain_db << " dB");
    if (info.rg_album_peak)
        MESSAGE("  RG album peak: " << *info.rg_album_peak);

    MESSAGE("  album art: " << info.album_art_width << "x" << info.album_art_height
            << " (" << info.album_art_rgba.size() << " bytes)");
}

TEST_CASE("MetadataReader - nonexistent file returns error") {
    auto result = MetadataReader::read("/tmp/impulse_nonexistent_file.mp3");
    CHECK(!result.has_value());
    CHECK(!result.error().empty());
}

TEST_CASE("MetadataReader - falls back to album art files in the track directory") {
    static constexpr std::string_view kTinyPngBase64 =
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4////fwAJ+wP9BUPFygAAAABJRU5ErkJggg==";

    const auto temp_root = prepareTempRoot("album_art_fallback");
    const auto copied_track = createTestWaveFile(temp_root, "fixture.wav");

    const auto external_art = temp_root / "Cover.png";
    writeBinaryFile(external_art, decodeBase64(kTinyPngBase64));

    auto result = MetadataReader::read(copied_track);
    REQUIRE(result.has_value());

    const TrackInfo& info = result.value();
    CHECK(info.album_art_width == 1);
    CHECK(info.album_art_height == 1);
    CHECK(info.album_art_rgba.size() == 4);
    CHECK(info.external_album_art_path == external_art);
    CHECK(ffmpegAnalysisValue(info, "Album art attached") == std::optional<std::string>{"no"});
    CHECK(ffmpegAnalysisValue(info, "Album art external file") ==
          std::optional<std::string>{external_art.filename().string()});
}

TEST_CASE("MetadataReader - can skip album art during bulk metadata scans") {
    static constexpr std::string_view kTinyPngBase64 =
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4////fwAJ+wP9BUPFygAAAABJRU5ErkJggg==";

    const auto temp_root = prepareTempRoot("album_art_skip");
    const auto copied_track = createTestWaveFile(temp_root, "fixture.wav");

    const auto external_art = temp_root / "Cover.png";
    writeBinaryFile(external_art, decodeBase64(kTinyPngBase64));

    auto result = MetadataReader::read(copied_track, MetadataReadOptions{.decode_album_art = false});
    REQUIRE(result.has_value());

    const TrackInfo& info = result.value();
    CHECK(info.album_art_width == 0);
    CHECK(info.album_art_height == 0);
    CHECK(info.album_art_rgba.empty());
    CHECK(info.external_album_art_path.empty());
    CHECK(!ffmpegAnalysisValue(info, "Album art attached").has_value());
    CHECK(!ffmpegAnalysisValue(info, "Album art external file").has_value());
}
