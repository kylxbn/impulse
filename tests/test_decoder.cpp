#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "audio/Decoder.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string_view>
#include <vector>

namespace {

const std::filesystem::path kTempRoot =
    std::filesystem::temp_directory_path() / "impulse_test_decoder";

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
