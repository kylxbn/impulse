#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "browser/FileBrowser.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

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

void writeBinaryFile(const std::filesystem::path& path,
                     const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out.good());
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    REQUIRE(out.good());
}

std::filesystem::path createSilentWaveFile(const std::filesystem::path& directory,
                                           std::string_view               filename) {
    constexpr uint16_t kChannels = 1;
    constexpr uint32_t kSampleRate = 8000;
    constexpr uint16_t kBitsPerSample = 16;
    constexpr uint16_t kBytesPerSample = kBitsPerSample / 8;
    constexpr uint32_t kFrameCount = 8;
    constexpr uint32_t kDataSize = kFrameCount * kChannels * kBytesPerSample;

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
    wav_bytes.resize(44 + kDataSize, 0);

    const auto path = directory / std::string(filename);
    writeBinaryFile(path, wav_bytes);
    return path;
}

}  // namespace

TEST_CASE("FileBrowser allows navigation outside the configured root and up to filesystem root") {
    const std::filesystem::path base = std::filesystem::temp_directory_path() / "impulse-file-browser-root";
    const std::filesystem::path child = base / "child";
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(child);

    FileBrowser browser(base);
    browser.navigate(child);
    REQUIRE(browser.currentPath() == std::filesystem::weakly_canonical(child));

    browser.navigate(base.parent_path());
    CHECK(browser.currentPath() == std::filesystem::weakly_canonical(base.parent_path()));

    browser.navigateUp();
    CHECK(browser.currentPath() == std::filesystem::weakly_canonical(base.parent_path().parent_path()));

    while (!browser.atFilesystemRoot())
        browser.navigateUp();
    CHECK(browser.currentPath() == browser.currentPath().root_path());

    browser.navigateUp();
    CHECK(browser.currentPath() == browser.currentPath().root_path());

    std::filesystem::remove_all(base);
}

TEST_CASE("FileBrowser changing root resets current path to the new root") {
    const std::filesystem::path base = std::filesystem::temp_directory_path() / "impulse-file-browser-change-root";
    const std::filesystem::path left = base / "left";
    const std::filesystem::path right = base / "right";
    const std::filesystem::path nested = left / "nested";
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(nested);
    std::filesystem::create_directories(right);

    FileBrowser browser(left);
    browser.navigate(nested);
    REQUIRE(browser.currentPath() == std::filesystem::weakly_canonical(nested));

    browser.setRoot(right);
    CHECK(browser.rootPath() == std::filesystem::weakly_canonical(right));
    CHECK(browser.currentPath() == std::filesystem::weakly_canonical(right));

    std::filesystem::remove_all(base);
}

TEST_CASE("FileBrowser recursive collection skips unreadable directories") {
    const std::filesystem::path base = std::filesystem::temp_directory_path() / "impulse-file-browser-perms";
    const std::filesystem::path readable = base / "readable";
    const std::filesystem::path blocked = base / "blocked";
    const std::filesystem::path readable_track = readable / "a.flac";
    const std::filesystem::path blocked_track = blocked / "b.flac";
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(readable);
    std::filesystem::create_directories(blocked);
    {
        std::ofstream out(readable_track);
        REQUIRE(out.good());
    }
    {
        std::ofstream out(blocked_track);
        REQUIRE(out.good());
    }

    std::error_code ec;
    std::filesystem::permissions(blocked,
                                 std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::remove,
                                 ec);
    REQUIRE_FALSE(ec);

    const auto paths = FileBrowser::collectAudioFiles(base, true);
    CHECK(std::ranges::find(paths, readable_track) != paths.end());

    std::filesystem::permissions(blocked,
                                 std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::add,
                                 ec);
    std::filesystem::remove_all(base);
}

TEST_CASE("FileBrowser recognises FFmpeg, VGM, and tracker-backed audio extensions") {
    CHECK(FileBrowser::isAudioFile("track.qoa"));
    CHECK(FileBrowser::isAudioFile("track.gsm"));
    CHECK(FileBrowser::isAudioFile("track.adx"));
    CHECK(FileBrowser::isAudioFile("track.oma"));
    CHECK(FileBrowser::isAudioFile("track.mp2"));
    CHECK(FileBrowser::isAudioFile("track.oga"));
    CHECK(FileBrowser::isAudioFile("track.m4b"));
    CHECK(FileBrowser::isAudioFile("track.aif"));
    CHECK(FileBrowser::isAudioFile("track.caf"));
    CHECK(FileBrowser::isAudioFile("track.mka"));
    CHECK(FileBrowser::isAudioFile("track.vgm"));
    CHECK(FileBrowser::isAudioFile("track.vgz"));
    CHECK(FileBrowser::isAudioFile("track.sap"));
    CHECK(FileBrowser::isAudioFile("track.sc68"));
    CHECK(FileBrowser::isAudioFile("track.sndh"));
    CHECK(FileBrowser::isAudioFile("track.snd"));
    CHECK(FileBrowser::isAudioFile("track.mod"));
    CHECK(FileBrowser::isAudioFile("track.xm"));
    CHECK(FileBrowser::isAudioFile("track.it"));
    CHECK(FileBrowser::isAudioFile("track.s3m"));
    CHECK_FALSE(FileBrowser::isAudioFile("track.txt"));
}

TEST_CASE("FileBrowser falls back to FFmpeg probing for oddball audio filenames") {
    const std::filesystem::path base = std::filesystem::temp_directory_path() / "impulse-file-browser-probe";
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(base);

    const auto disguised_wave = createSilentWaveFile(base, "mystery.nerd");
    const auto text_file = base / "notes.nerd";
    {
        std::ofstream out(text_file);
        REQUIRE(out.good());
        out << "not audio";
    }

    CHECK(FileBrowser::isAudioFile(disguised_wave));
    CHECK_FALSE(FileBrowser::isAudioFile(text_file));

    std::filesystem::remove_all(base);
}
