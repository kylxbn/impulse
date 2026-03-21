#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "audio/Decoder.hpp"
#include "audio/DecoderProvider.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <numbers>
#include <string_view>
#include <vector>

namespace {

const std::filesystem::path kTempRoot =
    std::filesystem::temp_directory_path() / "impulse_test_decoder";

constexpr std::string_view kMinimalVgmHex =
    "56676d2040000000500100000000000000000000000000001000000000000000"
    "00000000000000000000000000000000000000000c0000000000000000000000"
    "61100066";

void writeBinaryFile(const std::filesystem::path& path,
                     const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out.good());
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    REQUIRE(out.good());
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
                                         std::string_view               filename,
                                         double                         frequency_hz) {
    constexpr uint16_t kChannels = 2;
    constexpr uint32_t kSampleRate = 44100;
    constexpr uint16_t kBitsPerSample = 16;
    constexpr uint16_t kBytesPerSample = kBitsPerSample / 8;
    constexpr uint32_t kFrameCount = kSampleRate / 4;
    constexpr uint32_t kDataSize = kFrameCount * kChannels * kBytesPerSample;
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
        const double angle = 2.0 * std::numbers::pi * frequency_hz * frame / kSampleRate;
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
            appendBe16(16); // 32 bytes of sample data
            bytes.push_back(0);   // finetune
            bytes.push_back(64);  // volume
            appendBe16(0);        // loop start
            appendBe16(16);       // loop length
        } else {
            appendAscii("", 22);
            appendBe16(0);
            bytes.push_back(0);
            bytes.push_back(0);
            appendBe16(0);
            appendBe16(0);
        }
    }

    bytes.push_back(1); // song length
    bytes.push_back(0); // restart
    bytes.resize(bytes.size() + 128, 0);
    bytes.insert(bytes.end(), {'M', '.', 'K', '.'});

    std::vector<uint8_t> pattern(1024, 0);
    constexpr uint16_t kPeriodC3 = 428;
    constexpr uint8_t kSampleIndex = 1;
    pattern[0] = static_cast<uint8_t>(((kSampleIndex >> 4) << 4) | ((kPeriodC3 >> 8) & 0x0Fu));
    pattern[1] = static_cast<uint8_t>(kPeriodC3 & 0xFFu);
    pattern[2] = static_cast<uint8_t>((kSampleIndex & 0x0Fu) << 4);
    pattern[3] = 0;
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

std::filesystem::path createSn76489ToneBurstVgmFile(const std::filesystem::path& directory,
                                                    std::string_view filename,
                                                    bool loop_at_end = false) {
    constexpr uint32_t kHeaderSize = 0x40;
    constexpr uint32_t kSn76489Clock = 3579545;
    constexpr uint32_t kToneFrames = 2048;
    constexpr uint32_t kSilentFrames = 2048;

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
    writeLe32At(0x18, kToneFrames + kSilentFrames);
    writeLe32At(0x34, 0x0000000Cu);
    if (loop_at_end) {
        writeLe32At(0x1C, static_cast<uint32_t>(kHeaderSize - 0x1Cu));
        writeLe32At(0x20, kToneFrames + kSilentFrames);
    }

    const std::vector<uint8_t> commands = {
        0x50, 0x8A,       // Tone channel 0: latch low nibble = 0xA.
        0x50, 0x00,       // Tone channel 0: high bits = 0.
        0x50, 0x90,       // Tone channel 0: volume = 0 (loudest).
        0x61, 0x00, 0x08, // Wait 2048 samples.
        0x50, 0x9F,       // Tone channel 0: volume = 15 (mute).
        0x61, 0x00, 0x08, // Wait another 2048 samples.
        0x66,             // End of data.
    };
    bytes.insert(bytes.end(), commands.begin(), commands.end());
    writeLe32At(0x04, static_cast<uint32_t>(bytes.size() - 4));

    const auto path = directory / std::string(filename);
    writeBinaryFile(path, bytes);
    return path;
}

float peakAbsSample(const std::vector<float>& pcm) {
    float peak = 0.0f;
    for (const float sample : pcm)
        peak = std::max(peak, std::abs(sample));
    return peak;
}

float meanAbsSample(const std::vector<float>& pcm) {
    if (pcm.empty())
        return 0.0f;
    float sum = 0.0f;
    for (const float sample : pcm)
        sum += std::abs(sample);
    return sum / static_cast<float>(pcm.size());
}

std::filesystem::path prepareTempRoot(std::string_view suite_name) {
    std::error_code cleanup_ec;
    std::filesystem::remove_all(kTempRoot, cleanup_ec);

    const auto path = kTempRoot / std::string(suite_name);
    REQUIRE(std::filesystem::create_directories(path));
    return path;
}

} // namespace

TEST_CASE("Decoder - sequential opens decode audio safely") {
    const auto temp_dir = prepareTempRoot("sequential_open");
    const auto first_track = createTestWaveFile(temp_dir, "first.wav", 440.0);
    const auto second_track = createTestWaveFile(temp_dir, "second.wav", 660.0);

    Decoder decoder;
    std::vector<float> pcm;

    auto first_open = decoder.open(first_track, 48000);
    REQUIRE(first_open.ok);
    CHECK(decoder.decodeNextFrames(pcm) > 0);
    CHECK(!pcm.empty());

    auto second_open = decoder.open(second_track, 48000);
    REQUIRE(second_open.ok);
    CHECK(decoder.decodeNextFrames(pcm) > 0);
    CHECK(!pcm.empty());
}

TEST_CASE("DecoderProvider selects specialized backends before FFmpeg fallback") {
    const MediaSource vgm_source = MediaSource::fromPath("fixture.vgz");
    const MediaSource mod_source = MediaSource::fromPath("fixture.mod");
    const MediaSource wav_source = MediaSource::fromPath("fixture.wav");
    const MediaSource url_source = MediaSource::fromUrl("https://example.com/live-stream");

    CHECK(decoderProviderForSource(vgm_source).name() == "libvgm");
    CHECK(decoderProviderForSource(mod_source).name() == "libopenmpt");
    CHECK(decoderProviderForSource(wav_source).name() == "FFmpeg");
    CHECK(decoderProviderForSource(url_source).name() == "FFmpeg");
    CHECK(Decoder::supportsGaplessForSource(vgm_source) == true);
    CHECK(Decoder::supportsGaplessForSource(mod_source) == true);
    CHECK(Decoder::supportsGaplessForSource(wav_source) == true);
    CHECK(Decoder::supportsGaplessForSource(url_source) == false);

    CHECK(decoderProviderForSource(vgm_source).capabilitiesForSource(vgm_source).can_seek == true);
    CHECK(decoderProviderForSource(mod_source).capabilitiesForSource(mod_source).can_seek == true);
    CHECK(decoderProviderForSource(wav_source).capabilitiesForSource(wav_source).can_seek == true);
    CHECK(decoderProviderForSource(url_source).capabilitiesForSource(url_source).can_seek == false);
}

TEST_CASE("Decoder capabilities reflect shared decoder contract") {
    const MediaSource wav_source = MediaSource::fromPath("fixture.wav");
    const MediaSource url_source = MediaSource::fromUrl("https://example.com/live-stream");

    CHECK(Decoder{}.capabilities().can_seek == false);
    CHECK(Decoder{}.capabilities().supports_gapless == false);

    const auto file_caps = Decoder::capabilitiesForSource(wav_source);
    CHECK(file_caps.can_seek == true);
    CHECK(file_caps.supports_gapless == true);

    const auto url_caps = Decoder::capabilitiesForSource(url_source);
    CHECK(url_caps.can_seek == false);
    CHECK(url_caps.supports_gapless == false);
}

TEST_CASE("Decoder - VGM backend decodes minimal VGM audio") {
    const auto temp_dir = prepareTempRoot("vgm_open");
    const auto vgm_track = createMinimalVgmFile(temp_dir, "fixture.vgm");

    Decoder decoder;
    std::vector<float> pcm;

    auto open_result = decoder.open(vgm_track, 48000);
    REQUIRE(open_result.ok);
    CHECK(decoder.trackInfo().decoder_name == "libvgm");
    CHECK(decoder.trackInfo().seekable == true);
    CHECK(decoder.trackInfo().sample_rate == 48000);
    CHECK(decoder.capabilities().can_seek == true);
    CHECK(decoder.supportsGaplessPreparation() == true);
    CHECK(decoder.capabilities().supports_gapless == true);
    CHECK(decoder.instantaneousBitrateBps() > 0);
    CHECK(decoder.totalFrames() > 0);
    CHECK(decoder.decodeNextFrames(pcm) > 0);
    CHECK(!pcm.empty());
}

TEST_CASE("Decoder - libopenmpt backend decodes minimal MOD audio") {
    const auto temp_dir = prepareTempRoot("openmpt_open");
    const auto mod_track = createMinimalModFile(temp_dir, "fixture.mod");

    Decoder decoder;
    std::vector<float> pcm;

    auto open_result = decoder.open(mod_track, 48000);
    REQUIRE(open_result.ok);
    CHECK(decoder.trackInfo().decoder_name == "libopenmpt");
    CHECK(decoder.trackInfo().seekable == true);
    CHECK(decoder.trackInfo().sample_rate == 48000);
    CHECK(decoder.capabilities().can_seek == true);
    CHECK(decoder.supportsGaplessPreparation() == true);
    CHECK(decoder.capabilities().supports_gapless == true);
    CHECK(decoder.instantaneousBitrateBps() > 0);
    CHECK(decoder.totalFrames() > 0);
    CHECK(decoder.decodeNextFrames(pcm) > 0);
    CHECK(!pcm.empty());
    CHECK(peakAbsSample(pcm) > 0.0005f);
}

TEST_CASE("Decoder - sequential opens can switch between VGM and FFmpeg backends") {
    const auto temp_dir = prepareTempRoot("mixed_backends");
    const auto vgm_track = createMinimalVgmFile(temp_dir, "fixture.vgm");
    const auto wav_track = createTestWaveFile(temp_dir, "fixture.wav", 440.0);

    Decoder decoder;
    std::vector<float> pcm;

    auto vgm_open = decoder.open(vgm_track, 48000);
    REQUIRE(vgm_open.ok);
    CHECK(decoder.decodeNextFrames(pcm) > 0);

    auto wav_open = decoder.open(wav_track, 48000);
    REQUIRE(wav_open.ok);
    CHECK(decoder.trackInfo().decoder_name == "FFmpeg");
    CHECK(decoder.capabilities().can_seek == true);
    CHECK(decoder.capabilities().supports_gapless == true);
    CHECK(decoder.decodeNextFrames(pcm) > 0);
}

TEST_CASE("Decoder - sequential opens can switch between libopenmpt and FFmpeg backends") {
    const auto temp_dir = prepareTempRoot("openmpt_ffmpeg_backends");
    const auto mod_track = createMinimalModFile(temp_dir, "fixture.mod");
    const auto wav_track = createTestWaveFile(temp_dir, "fixture.wav", 440.0);

    Decoder decoder;
    std::vector<float> pcm;

    auto mod_open = decoder.open(mod_track, 48000);
    REQUIRE(mod_open.ok);
    CHECK(decoder.trackInfo().decoder_name == "libopenmpt");
    CHECK(decoder.decodeNextFrames(pcm) > 0);

    auto wav_open = decoder.open(wav_track, 48000);
    REQUIRE(wav_open.ok);
    CHECK(decoder.trackInfo().decoder_name == "FFmpeg");
    CHECK(decoder.capabilities().can_seek == true);
    CHECK(decoder.capabilities().supports_gapless == true);
    CHECK(decoder.decodeNextFrames(pcm) > 0);
}

TEST_CASE("Decoder - VGM backend clears stale mix buffers between decode calls") {
    const auto temp_dir = prepareTempRoot("vgm_progression");
    const auto vgm_track = createSn76489ToneBurstVgmFile(temp_dir, "tone-burst.vgm");

    Decoder decoder;
    std::vector<float> first_chunk;
    std::vector<float> second_chunk;

    auto open_result = decoder.open(vgm_track, 48000);
    REQUIRE(open_result.ok);

    REQUIRE(decoder.decodeNextFrames(first_chunk) > 0);
    REQUIRE(decoder.decodeNextFrames(second_chunk) > 0);

    REQUIRE(!first_chunk.empty());
    REQUIRE(!second_chunk.empty());
    CHECK(peakAbsSample(first_chunk) > 0.0005f);
    CHECK(meanAbsSample(second_chunk) < meanAbsSample(first_chunk) * 0.25f);
}

TEST_CASE("Decoder - looping VGM stops at first loop boundary") {
    const auto temp_dir = prepareTempRoot("vgm_loop_stop");
    const auto vgm_track = createSn76489ToneBurstVgmFile(temp_dir, "looping-tone-burst.vgm", true);

    Decoder decoder;
    std::vector<float> pcm;

    auto open_result = decoder.open(vgm_track, 48000);
    REQUIRE(open_result.ok);
    CHECK(decoder.supportsGaplessPreparation() == true);

    bool reached_eof = false;
    for (int iteration = 0; iteration < 8; ++iteration) {
        const int result = decoder.decodeNextFrames(pcm);
        REQUIRE(result >= 0);
        if (result == 0) {
            reached_eof = true;
            break;
        }
    }

    CHECK(reached_eof == true);
}

TEST_CASE("Decoder - VGM backend can seek by sample position") {
    const auto temp_dir = prepareTempRoot("vgm_seek");
    const auto vgm_track = createSn76489ToneBurstVgmFile(temp_dir, "seek-tone-burst.vgm");

    Decoder decoder;
    std::vector<float> first_chunk;
    std::vector<float> sought_chunk;

    auto open_result = decoder.open(vgm_track, 48000);
    REQUIRE(open_result.ok);
    REQUIRE(decoder.trackInfo().seekable == true);

    REQUIRE(decoder.decodeNextFrames(first_chunk) > 0);
    REQUIRE(decoder.seek(0.065));
    REQUIRE(decoder.decodeNextFrames(sought_chunk) > 0);

    CHECK(meanAbsSample(first_chunk) > 0.001f);
    CHECK(meanAbsSample(sought_chunk) < meanAbsSample(first_chunk) * 0.25f);
}

TEST_CASE("Decoder - libopenmpt backend can seek by time position") {
    const auto temp_dir = prepareTempRoot("openmpt_seek");
    const auto mod_track = createMinimalModFile(temp_dir, "seek.mod");

    Decoder decoder;
    std::vector<float> first_chunk;
    std::vector<float> sought_chunk;

    auto open_result = decoder.open(mod_track, 48000);
    REQUIRE(open_result.ok);
    REQUIRE(decoder.trackInfo().seekable == true);

    REQUIRE(decoder.decodeNextFrames(first_chunk) > 0);
    REQUIRE(decoder.seek(1.0));
    REQUIRE(decoder.decodeNextFrames(sought_chunk) > 0);

    CHECK(meanAbsSample(first_chunk) > 0.0001f);
    CHECK(meanAbsSample(sought_chunk) > 0.0001f);
}

TEST_CASE("Decoder - failed open leaves decoder in a closed state") {
    Decoder decoder;
    std::vector<float> pcm;

    auto open_result = decoder.open(std::filesystem::path("/tmp/impulse_missing_fixture.vgm"), 48000);
    CHECK(open_result.ok == false);
    CHECK(decoder.is_open() == false);
    CHECK(decoder.decodeNextFrames(pcm) == -1);
    CHECK(decoder.trackInfo().decoder_name.empty());
    CHECK(decoder.capabilities().can_seek == false);
    CHECK(decoder.capabilities().supports_gapless == false);
    CHECK(decoder.supportsGaplessPreparation() == false);
}
