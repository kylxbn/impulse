#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "app/SettingsStorage.hpp"

#include <filesystem>

TEST_CASE("SettingsStorage round-trips app settings") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "impulse-settings-test.txt";
    std::filesystem::remove(path);

    AppSettings defaults;
    defaults.browser_root_path = "/music/default";

    AppSettings settings;
    settings.browser_root_path = "/music/library";
    settings.buffer_ahead_seconds = 7.5f;
    settings.replaygain_preamp_with_rg_db = 1.5f;
    settings.replaygain_preamp_without_rg_db = -2.25f;
    settings.replaygain_mode = ReplayGain::GainMode::Album;
    settings.repeat_mode = RepeatMode::Track;
    settings.plr_reference_lufs = -23.0f;

    REQUIRE(SettingsStorage::save(path, settings));

    const AppSettings loaded = SettingsStorage::loadOrDefault(path, defaults);
    CHECK(loaded.browser_root_path == settings.browser_root_path);
    CHECK(loaded.buffer_ahead_seconds == doctest::Approx(settings.buffer_ahead_seconds));
    CHECK(loaded.replaygain_preamp_with_rg_db == doctest::Approx(settings.replaygain_preamp_with_rg_db));
    CHECK(loaded.replaygain_preamp_without_rg_db == doctest::Approx(settings.replaygain_preamp_without_rg_db));
    CHECK(loaded.replaygain_mode == settings.replaygain_mode);
    CHECK(loaded.repeat_mode == settings.repeat_mode);
    CHECK(loaded.plr_reference_lufs == doctest::Approx(settings.plr_reference_lufs));

    std::filesystem::remove(path);
}

TEST_CASE("SettingsStorage round-trips ReplayGain none mode") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "impulse-settings-replaygain-none.txt";
    std::filesystem::remove(path);

    AppSettings settings;
    settings.replaygain_mode = ReplayGain::GainMode::None;

    REQUIRE(SettingsStorage::save(path, settings));

    const AppSettings loaded = SettingsStorage::loadOrDefault(path, AppSettings{});
    CHECK(loaded.replaygain_mode == ReplayGain::GainMode::None);

    std::filesystem::remove(path);
}

TEST_CASE("SettingsStorage returns defaults when the file is missing") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "impulse-settings-missing.txt";
    std::filesystem::remove(path);

    AppSettings defaults;
    defaults.browser_root_path = "/music/default";
    defaults.buffer_ahead_seconds = 6.5f;
    defaults.replaygain_preamp_with_rg_db = 0.75f;
    defaults.replaygain_preamp_without_rg_db = -1.0f;
    defaults.replaygain_mode = ReplayGain::GainMode::Album;
    defaults.repeat_mode = RepeatMode::Playlist;
    defaults.plr_reference_lufs = -23.0f;

    const AppSettings loaded = SettingsStorage::loadOrDefault(path, defaults);
    CHECK(loaded.browser_root_path == defaults.browser_root_path);
    CHECK(loaded.buffer_ahead_seconds == doctest::Approx(defaults.buffer_ahead_seconds));
    CHECK(loaded.replaygain_preamp_with_rg_db == doctest::Approx(defaults.replaygain_preamp_with_rg_db));
    CHECK(loaded.replaygain_preamp_without_rg_db == doctest::Approx(defaults.replaygain_preamp_without_rg_db));
    CHECK(loaded.replaygain_mode == defaults.replaygain_mode);
    CHECK(loaded.repeat_mode == defaults.repeat_mode);
    CHECK(loaded.plr_reference_lufs == doctest::Approx(defaults.plr_reference_lufs));
}

TEST_CASE("SettingsStorage save replaces the file without leaving a temp file behind") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "impulse-settings-atomic.txt";
    const std::filesystem::path temp_path = path.string() + ".tmp";
    std::filesystem::remove(path);
    std::filesystem::remove(temp_path);

    AppSettings settings;
    settings.browser_root_path = "/music/library";
    settings.buffer_ahead_seconds = 5.0f;

    REQUIRE(SettingsStorage::save(path, settings));
    CHECK(std::filesystem::exists(path));
    CHECK_FALSE(std::filesystem::exists(temp_path));

    std::filesystem::remove(path);
}
