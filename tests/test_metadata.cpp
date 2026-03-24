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

const std::filesystem::path kSc68SamplePath = IMPULSE_TEST_SC68_SAMPLE;

const std::filesystem::path kTempRoot =
    std::filesystem::temp_directory_path() / "impulse_test_metadata_reader";

constexpr std::string_view kMinimalVgmHex =
    "56676d2040000000500100000000000000000000000000001000000000000000"
    "00000000000000000000000000000000000000000c0000000000000000000000"
    "61100066";

constexpr std::string_view kMinimalVgzHex =
    "1f8b08000000000002ff0b4bcf55706060600860644001020cb8010f123b518021"
    "0d008210a12844000000";

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

std::vector<uint8_t> decodeHex(std::string_view input) {
    auto decodeNibble = [](char ch) -> uint8_t {
        if (ch >= '0' && ch <= '9') return static_cast<uint8_t>(ch - '0');
        if (ch >= 'a' && ch <= 'f') return static_cast<uint8_t>(10 + ch - 'a');
        if (ch >= 'A' && ch <= 'F') return static_cast<uint8_t>(10 + ch - 'A');
        return 0;
    };

    std::vector<uint8_t> output;
    output.reserve(input.size() / 2);
    for (size_t i = 0; i + 1 < input.size(); i += 2) {
        output.push_back(static_cast<uint8_t>(
            (decodeNibble(input[i]) << 4) | decodeNibble(input[i + 1])));
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

std::filesystem::path createMinimalVgmFile(const std::filesystem::path& directory,
                                           std::string_view filename) {
    const auto path = directory / std::string(filename);
    writeBinaryFile(path, decodeHex(kMinimalVgmHex));
    return path;
}

std::filesystem::path createMinimalVgzFile(const std::filesystem::path& directory,
                                           std::string_view filename) {
    const auto path = directory / std::string(filename);
    writeBinaryFile(path, decodeHex(kMinimalVgzHex));
    return path;
}

std::filesystem::path createMinimalModFile(const std::filesystem::path& directory,
                                           std::string_view filename) {
    std::vector<uint8_t> bytes;
    bytes.reserve(20 + 31 * 30 + 2 + 128 + 4 + 1024 + 32);

    const auto appendAscii = [&](std::string_view value, size_t width) {
        bytes.insert(bytes.end(), value.begin(), value.end());
        bytes.resize(bytes.size() + (width - std::min(width, value.size())), 0);
    };

    const auto appendBe16 = [&](uint16_t value) {
        bytes.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
        bytes.push_back(static_cast<uint8_t>(value & 0xFFu));
    };

    appendAscii("Test Module", 20);
    for (int index = 0; index < 31; ++index) {
        if (index == 0) {
            appendAscii("Square", 22);
            appendBe16(16);
            bytes.push_back(0);
            bytes.push_back(64);
            appendBe16(0);
            appendBe16(16);
        } else {
            appendAscii("", 22);
            appendBe16(0);
            bytes.push_back(0);
            bytes.push_back(0);
            appendBe16(0);
            appendBe16(0);
        }
    }

    bytes.push_back(1);
    bytes.push_back(0);
    bytes.resize(bytes.size() + 128, 0);
    bytes.insert(bytes.end(), {'M', '.', 'K', '.'});

    std::vector<uint8_t> pattern(1024, 0);
    constexpr uint16_t kPeriodC3 = 428;
    constexpr uint8_t kSampleIndex = 1;
    pattern[0] = static_cast<uint8_t>(((kSampleIndex >> 4) << 4) | ((kPeriodC3 >> 8) & 0x0Fu));
    pattern[1] = static_cast<uint8_t>(kPeriodC3 & 0xFFu);
    pattern[2] = static_cast<uint8_t>((kSampleIndex & 0x0Fu) << 4);
    bytes.insert(bytes.end(), pattern.begin(), pattern.end());

    const std::vector<uint8_t> sample_data = {
        0, 96, 127, 96, 0, 160, 128, 160,
        0, 96, 127, 96, 0, 160, 128, 160,
        0, 96, 127, 96, 0, 160, 128, 160,
        0, 96, 127, 96, 0, 160, 128, 160,
    };
    bytes.insert(bytes.end(), sample_data.begin(), sample_data.end());

    const auto path = directory / std::string(filename);
    writeBinaryFile(path, bytes);
    return path;
}

void appendUtf16Le(std::vector<uint8_t>& bytes, std::string_view text) {
    for (const unsigned char ch : text)
        appendLe16(bytes, ch);
    appendLe16(bytes, 0);
}

std::filesystem::path createTaggedVgmFile(const std::filesystem::path& directory,
                                          std::string_view filename) {
    constexpr uint32_t kHeaderSize = 0x40;
    constexpr uint32_t kSn76489Clock = 3579545;
    constexpr uint32_t kTrackFrames = 4096;

    std::vector<uint8_t> bytes(kHeaderSize, 0);
    bytes[0] = 'V';
    bytes[1] = 'g';
    bytes[2] = 'm';
    bytes[3] = ' ';

    auto writeLe32At = [&](size_t offset, uint32_t value) {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xFFu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
        bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
        bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
    };

    writeLe32At(0x08, 0x00000150u);
    writeLe32At(0x0C, kSn76489Clock);
    writeLe32At(0x18, kTrackFrames);
    writeLe32At(0x34, 0x0000000Cu);

    const std::vector<uint8_t> commands = {
        0x50, 0x90,
        0x61, 0x00, 0x10,
        0x66,
    };
    bytes.insert(bytes.end(), commands.begin(), commands.end());

    const uint32_t gd3_offset = static_cast<uint32_t>(bytes.size());
    bytes.insert(bytes.end(), {'G', 'd', '3', ' '});
    appendLe32(bytes, 0x00000100u);

    std::vector<uint8_t> gd3_payload;
    appendUtf16Le(gd3_payload, "Green Hill Zone");
    appendUtf16Le(gd3_payload, "");
    appendUtf16Le(gd3_payload, "Sonic the Hedgehog");
    appendUtf16Le(gd3_payload, "Sonic the Hedgehog");
    appendUtf16Le(gd3_payload, "Mega Drive");
    appendUtf16Le(gd3_payload, "Mega Drive");
    appendUtf16Le(gd3_payload, "Masato Nakamura");
    appendUtf16Le(gd3_payload, "");
    appendUtf16Le(gd3_payload, "1991");
    appendUtf16Le(gd3_payload, "Test Encoder");
    appendUtf16Le(gd3_payload, "Opening theme");

    appendLe32(bytes, static_cast<uint32_t>(gd3_payload.size()));
    bytes.insert(bytes.end(), gd3_payload.begin(), gd3_payload.end());

    writeLe32At(0x04, static_cast<uint32_t>(bytes.size() - 4));
    writeLe32At(0x14, gd3_offset - 0x14u);

    const auto path = directory / std::string(filename);
    writeBinaryFile(path, bytes);
    return path;
}

std::filesystem::path prepareTempRoot(std::string_view suite_name) {
    std::error_code cleanup_ec;
    std::filesystem::remove_all(kTempRoot, cleanup_ec);

    const auto path = kTempRoot / std::string(suite_name);
    REQUIRE(std::filesystem::create_directories(path));
    return path;
}

std::optional<std::string> decoderAnalysisValue(const TrackInfo& info,
                                                std::string_view label) {
    for (const auto& field : info.decoder_analysis) {
        if (field.label == label)
            return field.value;
    }
    return std::nullopt;
}

std::optional<std::string> formatMetadataValue(const TrackInfo& info,
                                               std::string_view label) {
    for (const auto& field : info.format_metadata) {
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
    CHECK(!info.decoder_name.empty());
    CHECK(!info.decoder_analysis.empty());
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
    CHECK(decoderAnalysisValue(info, "Album art attached") == std::optional<std::string>{"no"});
    CHECK(decoderAnalysisValue(info, "Album art external file") ==
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
    CHECK(!decoderAnalysisValue(info, "Album art attached").has_value());
    CHECK(!decoderAnalysisValue(info, "Album art external file").has_value());
}

TEST_CASE("MetadataReader - falls back to sidecar lyrics files in the track directory") {
    const auto temp_root = prepareTempRoot("lyrics_fallback");
    const auto copied_track = createTestWaveFile(temp_root, "fixture.wav");
    const auto sidecar_lyrics = temp_root / "fixture.lrc";

    {
        std::ofstream out(sidecar_lyrics, std::ios::binary);
        REQUIRE(out.good());
        out << "[00:01.00]Hello\n[00:03.00]world";
        REQUIRE(out.good());
    }

    auto result = MetadataReader::read(copied_track);
    REQUIRE(result.has_value());

    const TrackInfo& info = result.value();
    CHECK(info.lyrics == "[00:01.00]Hello\n[00:03.00]world");
    CHECK(info.lyrics_source_kind == LyricsSourceKind::SidecarFile);
    CHECK(info.lyrics_source_path == sidecar_lyrics);
    CHECK(decoderAnalysisValue(info, "Lyrics source") ==
          std::optional<std::string>{std::format("sidecar file ({})", sidecar_lyrics.filename().string())});
}

TEST_CASE("MetadataReader - consecutive reads reuse the same external album art safely") {
    static constexpr std::string_view kTinyPngBase64 =
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4////fwAJ+wP9BUPFygAAAABJRU5ErkJggg==";

    const auto temp_root = prepareTempRoot("album_art_consecutive_reads");
    const auto first_track = createTestWaveFile(temp_root, "track-a.wav");
    const auto second_track = createTestWaveFile(temp_root, "track-b.wav");
    const auto external_art = temp_root / "Cover.png";
    writeBinaryFile(external_art, decodeBase64(kTinyPngBase64));

    auto first_result = MetadataReader::read(first_track);
    REQUIRE(first_result.has_value());
    CHECK(first_result->album_art_width == 1);
    CHECK(first_result->album_art_height == 1);
    CHECK(first_result->external_album_art_path == external_art);
    CHECK(first_result->album_art_rgba.size() == 4);

    auto second_result = MetadataReader::read(second_track);
    REQUIRE(second_result.has_value());
    CHECK(second_result->album_art_width == 1);
    CHECK(second_result->album_art_height == 1);
    CHECK(second_result->external_album_art_path == external_art);
    CHECK(second_result->album_art_rgba.size() == 4);
}

TEST_CASE("MetadataReader - reads VGM and VGZ through libvgm") {
    const auto temp_root = prepareTempRoot("vgm_metadata");
    const auto vgm_path = createMinimalVgmFile(temp_root, "fixture.vgm");
    const auto vgz_path = createMinimalVgzFile(temp_root, "fixture.vgz");

    auto vgm_result = MetadataReader::read(vgm_path, MetadataReadOptions{.decode_album_art = false});
    REQUIRE(vgm_result.has_value());
    CHECK(vgm_result->decoder_name == "libvgm");
    CHECK(vgm_result->codec_name == "VGM");
    CHECK(vgm_result->container_format == "vgm");
    CHECK(vgm_result->seekable == true);
    CHECK(vgm_result->sample_rate == 44100);
    CHECK(vgm_result->bitrate_bps > 0);
    CHECK(vgm_result->channels == 2);
    CHECK(vgm_result->channel_layout == "stereo");
    CHECK(vgm_result->duration_seconds > 0.0);
    CHECK(vgm_result->title.empty());
    CHECK(vgm_result->decoder_analysis.size() >= 3);
    CHECK(decoderAnalysisValue(*vgm_result, "Gapless preload") ==
          std::optional<std::string>{"supported"});
    CHECK(decoderAnalysisValue(*vgm_result, "Seek support") ==
          std::optional<std::string>{"enabled via libvgm sample seek"});

    auto vgz_result = MetadataReader::read(vgz_path, MetadataReadOptions{.decode_album_art = false});
    REQUIRE(vgz_result.has_value());
    CHECK(vgz_result->decoder_name == "libvgm");
    CHECK(vgz_result->container_format == "vgz");
    CHECK(vgz_result->bitrate_bps > 0);
    CHECK(vgz_result->duration_seconds > 0.0);
}

TEST_CASE("MetadataReader - reads tracker modules through libopenmpt") {
    const auto temp_root = prepareTempRoot("openmpt_metadata");
    const auto mod_path = createMinimalModFile(temp_root, "fixture.mod");

    auto result = MetadataReader::read(mod_path, MetadataReadOptions{.decode_album_art = false});
    REQUIRE(result.has_value());

    const TrackInfo& info = *result;
    CHECK(info.decoder_name == "libopenmpt");
    CHECK(info.codec_name == "ProTracker MOD (M.K.)");
    CHECK(info.container_format == "mod");
    CHECK(info.seekable == true);
    CHECK(info.sample_rate == 48000);
    CHECK(info.bitrate_bps > 0);
    CHECK(info.channels == 2);
    CHECK(info.channel_layout == "stereo");
    CHECK(info.duration_seconds > 0.0);
    CHECK(info.title == "Test Module");
    CHECK(info.decoder_analysis.size() >= 6);
    CHECK(decoderAnalysisValue(info, "Gapless preload") ==
          std::optional<std::string>{"supported"});
    CHECK(decoderAnalysisValue(info, "Seek support") ==
          std::optional<std::string>{"enabled via libopenmpt time seek"});
    CHECK(decoderAnalysisValue(info, "Interpolation") ==
          std::optional<std::string>{"disabled (1-tap / no interpolation)"});
    CHECK(formatMetadataValue(info, "Format") ==
          std::optional<std::string>{"mod"});
    CHECK(formatMetadataValue(info, "Tracker") ==
          std::optional<std::string>{"Generic ProTracker or compatible"});
}

TEST_CASE("MetadataReader - reads Atari ST SNDH through libsc68") {
    REQUIRE(std::filesystem::exists(kSc68SamplePath));

    auto result = MetadataReader::read(kSc68SamplePath, MetadataReadOptions{.decode_album_art = false});
    REQUIRE(result.has_value());

    const TrackInfo& info = *result;
    CHECK(info.decoder_name == "libsc68");
    CHECK(info.container_format == "sndh");
    CHECK(info.seekable == false);
    CHECK(info.sample_rate == 48000);
    CHECK(info.bit_depth == 16);
    CHECK(info.channels == 2);
    CHECK(info.channel_layout == "stereo");
    CHECK(info.duration_seconds > 0.0);
    CHECK(info.bitrate_bps > 0);
    CHECK(info.track_number == "1");
    CHECK(decoderAnalysisValue(info, "YM engine") ==
          std::optional<std::string>{"blep"});
    CHECK(decoderAnalysisValue(info, "PCM format") ==
          std::optional<std::string>{"s16"});
    CHECK(decoderAnalysisValue(info, "Paula interpolation") ==
          std::optional<std::string>{"disabled"});
    CHECK(decoderAnalysisValue(info, "Seek support") ==
          std::optional<std::string>{"disabled"});
    CHECK(decoderAnalysisValue(info, "Gapless preload") ==
          std::optional<std::string>{"not supported"});
}

TEST_CASE("MetadataReader - reads GD3 tags and chip analysis from VGM") {
    const auto temp_root = prepareTempRoot("vgm_tagged_metadata");
    const auto tagged_vgm = createTaggedVgmFile(temp_root, "tagged.vgm");

    auto result = MetadataReader::read(tagged_vgm, MetadataReadOptions{.decode_album_art = false});
    REQUIRE(result.has_value());

    const TrackInfo& info = *result;
    CHECK(info.decoder_name == "libvgm");
    CHECK(info.title == "Green Hill Zone");
    CHECK(info.album == "Sonic the Hedgehog");
    CHECK(info.genre == "Mega Drive");
    CHECK(info.artist == "Masato Nakamura");
    CHECK(info.year == "1991");
    CHECK(info.comment == "Opening theme");
    CHECK(info.sample_rate == 44100);
    CHECK(info.bitrate_bps > 0);
    CHECK(formatMetadataValue(info, "Encoded By") ==
          std::optional<std::string>{"Test Encoder"});
    CHECK(formatMetadataValue(info, "Game") ==
          std::optional<std::string>{"Sonic the Hedgehog"});
    CHECK(decoderAnalysisValue(info, "Chip summary").has_value());
    CHECK(decoderAnalysisValue(info, "Playback stop behavior") ==
          std::optional<std::string>{"stop at end of stream"});
}
