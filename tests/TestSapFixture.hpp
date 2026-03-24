#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>

inline std::filesystem::path createMinimalSapFile(const std::filesystem::path& directory,
                                                  std::string_view               filename) {
    static constexpr std::string_view kSapHeader =
        "SAP\r\n"
        "AUTHOR \"Impulse Test\"\r\n"
        "NAME \"Tone\"\r\n"
        "DATE \"2026\"\r\n"
        "TYPE B\r\n"
        "INIT 0600\r\n"
        "PLAYER 0610\r\n"
        "TIME 00:01.000\r\n";

    static constexpr std::array<uint8_t, 23> kSapBody = {
        0xFF, 0xFF,
        0x00, 0x06,
        0x10, 0x06,
        0xA9, 0x03,
        0x8D, 0x0F, 0xD2,
        0xA9, 0x28,
        0x8D, 0x00, 0xD2,
        0xA9, 0xAF,
        0x8D, 0x01, 0xD2,
        0x60,
        0x60,
    };

    const auto path = directory / std::string(filename);
    std::ofstream out(path, std::ios::binary);
    out.write(kSapHeader.data(), static_cast<std::streamsize>(kSapHeader.size()));
    out.write(reinterpret_cast<const char*>(kSapBody.data()),
              static_cast<std::streamsize>(kSapBody.size()));
    return path;
}
