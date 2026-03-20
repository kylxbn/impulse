#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "browser/FileBrowser.hpp"

#include <filesystem>
#include <fstream>

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

TEST_CASE("FileBrowser recognises VGM and VGZ files as audio") {
    CHECK(FileBrowser::isAudioFile("track.vgm"));
    CHECK(FileBrowser::isAudioFile("track.vgz"));
    CHECK_FALSE(FileBrowser::isAudioFile("track.txt"));
}
