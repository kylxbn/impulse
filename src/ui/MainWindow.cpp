#include "MainWindow.hpp"

#include "app/SettingsStorage.hpp"
#include "app/SessionStorage.hpp"
#include "core/Lyrics.hpp"
#include "metadata/MetadataReader.hpp"
#include "playlist/M3U8Playlist.hpp"
#include "ui/PlaybackNavigationUtils.hpp"

#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <imgui_internal.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <mutex>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>

namespace {

constexpr ImGuiID kBrowserSortName = 1;
constexpr std::string_view kUiFontPath = "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc";
constexpr ImU32 kUiFontFaceIndex = 0; // Noto Sans CJK JP
constexpr float kUiFontSize = 18.0f;
constexpr float kMinVolumeDb = -60.0f;
constexpr float kMaxVolumeDb = 0.0f;
constexpr float kMinBufferAheadSeconds = 1.0f;
constexpr float kMaxBufferAheadSeconds = 16.0f;
constexpr float kTransportPanelMinHeight = 200.0f;

#ifdef IMPULSE_APP_VERSION
constexpr std::string_view kAppVersion = IMPULSE_APP_VERSION;
#else
constexpr std::string_view kAppVersion = "dev";
#endif
constexpr std::string_view kAppTagline = "A technical Linux-native desktop music player.";
constexpr std::string_view kProjectLicenseName = "GNU GPL v3";
constexpr std::string_view kProjectLicenseSummary =
    "Impulse is free software licensed under the GNU General Public License version 3.";
constexpr std::string_view kProjectWarrantySummary =
    "This program comes with absolutely no warranty.";
constexpr std::string_view kProjectRedistributionSummary =
    "You may copy, modify, and redistribute it under the terms of GPLv3.";
constexpr std::string_view kProjectLicenseLocation =
    "See the bundled LICENSE file or your installed package metadata for the full license text.";

std::filesystem::path defaultMusicPath() {
    if (const char* home = std::getenv("HOME")) {
        std::filesystem::path home_path(home);
        std::filesystem::path music_path = home_path / "Music";
        if (std::filesystem::exists(music_path))
            return music_path;
        return home_path;
    }
    return std::filesystem::current_path();
}

std::filesystem::path defaultHomePath() {
    if (const char* home = std::getenv("HOME")) {
        std::filesystem::path home_path(home);
        if (std::filesystem::exists(home_path))
            return home_path;
    }
    return std::filesystem::current_path();
}

AppSettings defaultAppSettings() {
    return AppSettings{
        .browser_root_path = defaultMusicPath(),
        .buffer_ahead_seconds = 16.0f,
        .replaygain_preamp_with_rg_db = 0.0f,
        .replaygain_preamp_without_rg_db = 0.0f,
        .replaygain_mode = ReplayGain::GainMode::Track,
        .repeat_mode = RepeatMode::Off,
        .plr_reference_lufs = -18.0f,
    };
}

float sanitizeBufferAheadSeconds(float seconds) {
    if (!std::isfinite(seconds))
        return 16.0f;
    return std::clamp(seconds, kMinBufferAheadSeconds, kMaxBufferAheadSeconds);
}

std::optional<std::filesystem::path> canonicalDirectory(const std::filesystem::path& path) {
    std::error_code ec;
    auto canonical_path = std::filesystem::weakly_canonical(path, ec);
    if (ec || canonical_path.empty() || !std::filesystem::is_directory(canonical_path, ec))
        return std::nullopt;
    return canonical_path;
}

template <size_t N>
void copyToBuffer(const std::string& value, std::array<char, N>& buffer) {
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
}

std::string toLower(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string playlistTabLabel(const PlaylistDocument& playlist, bool is_playing) {
    return std::format("{}{}###playlist-tab-{}",
                       is_playing ? "* " : "",
                       playlist.name,
                       playlist.id);
}

template <typename T, typename U>
bool sameSharedOwner(const std::shared_ptr<T>& lhs,
                     const std::shared_ptr<U>& rhs) {
    return !lhs.owner_before(rhs) && !rhs.owner_before(lhs);
}

ReplayGain::GainApplication replayGainApplicationForItem(const PlaylistItem& item,
                                                         const AppSettings& settings) {
    return ReplayGain::gainApplication(item.track_replay_gain_db,
                                       item.album_replay_gain_db,
                                       item.track_replay_gain_peak,
                                       item.album_replay_gain_peak,
                                       settings.replayGainSettings());
}

const ImVec4 kReplayGainClipTextColor(0.92f, 0.32f, 0.28f, 1.0f);

std::string playlistFileDialogName(std::string_view playlist_name) {
    std::string sanitized;
    sanitized.reserve(playlist_name.size());
    for (char c : playlist_name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')
            sanitized.push_back(c);
        else if (std::isspace(static_cast<unsigned char>(c)))
            sanitized.push_back('-');
    }

    if (sanitized.empty())
        sanitized = "playlist";

    return sanitized + ".m3u8";
}

constexpr SDL_DialogFileFilter kPlaylistDialogFilters[] = {
    {"M3U8 playlist", "m3u8;m3u"},
};

bool loadUiFont(ImGuiIO& io) {
    if (!std::filesystem::exists(kUiFontPath))
        return false;

    ImFontConfig font_cfg{};
    font_cfg.FontNo = kUiFontFaceIndex;
    font_cfg.PixelSnapH = true;

    ImFont* font = io.Fonts->AddFontFromFileTTF(kUiFontPath.data(),
                                                kUiFontSize,
                                                &font_cfg,
                                                io.Fonts->GetGlyphRangesChineseFull());
    if (font == nullptr)
        return false;

    io.FontDefault = font;
    return true;
}

std::string formatTime(double seconds) {
    int total = std::max(0, static_cast<int>(seconds));
    int h = total / 3600;
    int m = (total % 3600) / 60;
    int s = total % 60;
    if (h > 0) return std::format("{:d}:{:02d}:{:02d}", h, m, s);
    return std::format("{:d}:{:02d}", m, s);
}

std::string formatBytes(int64_t bytes) {
    constexpr double kib = 1024.0;
    constexpr double mib = kib * 1024.0;
    constexpr double gib = mib * 1024.0;
    if (bytes >= static_cast<int64_t>(gib))
        return std::format("{:.2f} GiB", static_cast<double>(bytes) / gib);
    if (bytes >= static_cast<int64_t>(mib))
        return std::format("{:.2f} MiB", static_cast<double>(bytes) / mib);
    if (bytes >= static_cast<int64_t>(kib))
        return std::format("{:.1f} KiB", static_cast<double>(bytes) / kib);
    return std::format("{} B", bytes);
}

std::string formatKbpsValue(int64_t bps) {
    if (bps <= 0) return "--";
    return std::format("{}", bps / 1000);
}

float linearGainToDb(float linear_gain) {
    if (linear_gain <= 0.0f)
        return kMinVolumeDb;

    return std::clamp(20.0f * std::log10(linear_gain), kMinVolumeDb, kMaxVolumeDb);
}

std::string formatVolumeDbLabel(float linear_gain) {
    const float clamped_gain = std::clamp(linear_gain, 0.0f, 1.0f);
    if (clamped_gain <= 0.0f)
        return "Muted";

    return std::format("{:.1f} dB", linearGainToDb(clamped_gain));
}

const char* replayGainModeLabel(ReplayGain::GainMode mode) {
    switch (mode) {
        case ReplayGain::GainMode::None:  return "None";
        case ReplayGain::GainMode::Album: return "Album gain";
        case ReplayGain::GainMode::Track:
        default:                          return "Track gain";
    }
}

const char* repeatModeLabel(RepeatMode mode) {
    switch (mode) {
        case RepeatMode::Playlist: return "Repeat playlist";
        case RepeatMode::Track:    return "Repeat track";
        case RepeatMode::Off:
        default:                   return "Repeat off";
    }
}

std::string formatTrackSummary(const TrackInfo& info) {
    std::string summary;

    auto append_part = [&](const std::string& part) {
        if (part.empty())
            return;
        if (!summary.empty())
            summary += " | ";
        summary += part;
    };

    append_part(info.codec_name);
    if (info.sample_rate > 0)
        append_part(std::format("{:.1f} kHz", static_cast<double>(info.sample_rate) / 1000.0));
    if (info.bit_depth > 0)
        append_part(std::format("{}-bit", info.bit_depth));
    if (info.channels > 0)
        append_part(info.channel_layout.empty() ? std::format("{} ch", info.channels) : info.channel_layout);
    if (info.bitrate_bps > 0)
        append_part(std::format("{} kbps", info.bitrate_bps / 1000));

    return summary;
}

bool renderTrackInfoFieldTable(const char* table_id,
                               const std::vector<TrackInfoField>& fields,
                               float label_width) {
    if (fields.empty())
        return false;

    if (!ImGui::BeginTable(table_id, 2,
                           ImGuiTableFlags_SizingStretchProp,
                           ImVec2(-1.0f, 0.0f))) {
        return false;
    }

    ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed, label_width);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

    for (const auto& field : fields) {
        if (field.label.empty() || field.value.empty())
            continue;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", field.label.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("%s", field.value.c_str());
    }

    ImGui::EndTable();
    return true;
}

const char* playbackStatusLabel(PlaybackStatus status) {
    switch (status) {
        case PlaybackStatus::Buffering:  return "Buffering...";
        case PlaybackStatus::Playing:    return "Playing";
        case PlaybackStatus::Paused:     return "Paused";
        case PlaybackStatus::Seeking:    return "Seeking...";
        case PlaybackStatus::EndOfTrack: return "End of track";
        case PlaybackStatus::Error:      return "Playback error";
        case PlaybackStatus::Stopped:
        default:                        return "Stopped";
    }
}

PlaylistSortDirection sortDirectionFromImGui(ImGuiSortDirection direction) {
    return direction == ImGuiSortDirection_Descending
        ? PlaylistSortDirection::Descending
        : PlaylistSortDirection::Ascending;
}

}  // namespace

MainWindow::MainWindow(Application& app, FileBrowser& browser)
    : app_(app),
      browser_(browser),
      dialog_callback_state_(std::make_shared<DialogCallbackState>()) {
    initSDL();
    initStoragePaths();
    loadSettings();
    loadSession();
    syncAllPlaylistRevisions();
    initImGui();
}

MainWindow::~MainWindow() {
    {
        std::scoped_lock lock(dialog_callback_state_->mutex);
        dialog_callback_state_->accepting_results = false;
        dialog_callback_state_->pending_results.clear();
    }
    mpris_.shutdown();
    saveSession();
    if (album_art_texture_) SDL_DestroyTexture(album_art_texture_);
    shutdownImGui();
    shutdownSDL();
}

void MainWindow::initSDL() {
    SDL_SetHint("SDL_APP_ID", "impulse");
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());

    window_ = SDL_CreateWindow("Impulse", 1200, 670,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_)
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_)
        throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());

    SDL_SetRenderVSync(renderer_, 1);
}

void MainWindow::initStoragePaths() {
    char* pref_path_c = SDL_GetPrefPath("Impulse", "impulse");
    std::filesystem::path pref_path = pref_path_c ? std::filesystem::path(pref_path_c)
                                                  : (std::filesystem::current_path() / ".impulse");
    if (pref_path_c) SDL_free(pref_path_c);

    std::error_code ec;
    std::filesystem::create_directories(pref_path, ec);

    session_path_ = pref_path / "session.txt";
    settings_path_ = pref_path / "settings.txt";
    imgui_ini_path_ = (pref_path / "imgui.ini").string();
    should_build_default_layout_ = !std::filesystem::exists(imgui_ini_path_);
}

void MainWindow::loadSettings() {
    const AppSettings defaults = defaultAppSettings();
    settings_ = SettingsStorage::loadOrDefault(settings_path_, defaults);

    auto browser_root = canonicalDirectory(settings_.browser_root_path);
    if (!browser_root)
        browser_root = canonicalDirectory(defaults.browser_root_path);
    settings_.browser_root_path = browser_root.value_or(defaults.browser_root_path);

    if (settings_.plr_reference_lufs >= 0.0f)
        settings_.plr_reference_lufs = defaults.plr_reference_lufs;
    settings_.buffer_ahead_seconds = sanitizeBufferAheadSeconds(settings_.buffer_ahead_seconds);

    browser_.setRoot(settings_.browser_root_path);
    for (auto& playlist : workspace_.playlists())
        playlist.playlist.setPlrReferenceLufs(settings_.plr_reference_lufs);
    app_.commandSetBufferAheadSeconds(settings_.buffer_ahead_seconds);
    app_.commandSetReplayGainSettings(settings_.replayGainSettings());
    syncSettingsFormFromCurrent();
}

void MainWindow::loadSession() {
    browser_selected_path_ = browser_.currentPath();

    if (auto state = SessionStorage::load(session_path_)) {
        std::error_code browser_ec;
        if (!state->browser_path.empty() &&
            std::filesystem::is_directory(state->browser_path, browser_ec) &&
            !browser_ec) {
            browser_.navigate(state->browser_path);
        }
        browser_selected_path_ = browser_.currentPath();

        workspace_.clear();
        playlist_view_states_.clear();
        for (const auto& session_playlist : state->playlists) {
            auto& playlist = workspace_.createPlaylist(session_playlist.name);
            playlist.playlist.setPlrReferenceLufs(settings_.plr_reference_lufs);
            playlist_view_states_.try_emplace(playlist.id);
            for (const auto& source : session_playlist.track_sources) {
                if (source.isFile()) {
                    std::error_code track_ec;
                    if (!std::filesystem::exists(source.path, track_ec) || track_ec)
                        continue;

                    std::optional<TrackInfo> metadata;
                    if (auto result = MetadataReader::read(
                            source, MetadataReadOptions{.decode_album_art = false});
                        result.has_value())
                        metadata = std::move(result.value());

                    playlist.playlist.addTrack(source, metadata ? &*metadata : nullptr);
                    continue;
                }

                playlist.playlist.addTrack(source, nullptr);
            }

            if (!playlist.playlist.empty()) {
                playlist.playlist.setCurrentIndex(
                    std::min(session_playlist.current_index, playlist.playlist.size() - 1));
            }
        }

        workspace_.ensureDefaultPlaylist();
        playlist_view_states_.try_emplace(workspace_.activePlaylistId());
        if (!workspace_.playlists().empty()) {
            const size_t active_index = std::min(state->active_playlist_index,
                                                 workspace_.playlists().size() - 1);
            workspace_.activatePlaylist(workspace_.playlists()[active_index].id);
        }
    } else {
        workspace_.ensureDefaultPlaylist();
        activePlaylist().setPlrReferenceLufs(settings_.plr_reference_lufs);
        playlist_view_states_.try_emplace(workspace_.activePlaylistId());
    }
}

void MainWindow::saveSession() const {
    SessionState state;
    state.browser_path = browser_.currentPath();
    for (size_t i = 0; i < workspace_.playlists().size(); ++i) {
        const auto& playlist = workspace_.playlists()[i];
        if (playlist.id == workspace_.activePlaylistId())
            state.active_playlist_index = i;

        SessionPlaylistState session_playlist;
        session_playlist.name = playlist.name;
        session_playlist.current_index = playlist.playlist.currentIndex();
        session_playlist.track_sources.reserve(playlist.playlist.size());
        for (const auto& track : playlist.playlist.tracks())
            session_playlist.track_sources.push_back(track.source);

        state.playlists.push_back(std::move(session_playlist));
    }

    SessionStorage::save(session_path_, state);
}

void MainWindow::shutdownSDL() {
    if (renderer_) { SDL_DestroyRenderer(renderer_); renderer_ = nullptr; }
    if (window_)   { SDL_DestroyWindow(window_);     window_   = nullptr; }
    SDL_Quit();
}

void MainWindow::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.IniFilename = imgui_ini_path_.c_str();

    float dpi_scale = SDL_GetWindowDisplayScale(window_);
    if (dpi_scale < 1.0f) dpi_scale = 1.0f;
    io.FontGlobalScale = dpi_scale;

    if (!loadUiFont(io))
        status_message_ = std::format("Failed to load UI font from {}. Using ImGui default font.", kUiFontPath);

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.FrameRounding = 6.0f;
    style.TabRounding = 6.0f;
    style.WindowMenuButtonPosition = ImGuiDir_Right;
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_);
    ImGui_ImplSDLRenderer3_Init(renderer_);
}

void MainWindow::shutdownImGui() {
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

bool MainWindow::runFrame() {
    const bool minimized = isWindowMinimized();
    const int wait_timeout_ms = nextWaitTimeoutMs(minimized);

    if (!pumpEvents(wait_timeout_ms))
        return false;

    frame_now_playing_ = app_.currentNowPlaying();
    syncPendingEndOfTrackAdvanceWithNowPlaying();
    processMprisCommands();

    const bool decoder_reached_eof = app_.decoderReachedEof();
    if (decoder_reached_eof) {
        if (!pending_end_of_track_advance_)
            scheduleGaplessAdvanceTrack();
        else if (const auto* playlist = playbackPlaylistDocument();
                 playlist &&
                 (app_.playbackStatus() == PlaybackStatus::EndOfTrack ||
                  app_.playbackStatus() == PlaybackStatus::Stopped)) {
            const auto next_index = playbackAdvanceIndex(*playlist);
            if (next_index) {
                const PlaylistItem& next = playlist->playlist.tracks()[*next_index];
                const bool can_gapless = frame_now_playing_ &&
                    frame_now_playing_->track_info &&
                    !frame_now_playing_->track_info->is_stream &&
                    next.source.isFile();
                if (!can_gapless)
                    openTrackAt(playlist->id, *next_index);
            }
        }
    } else {
        clearPendingEndOfTrackAdvance();
    }

    const auto playback_status = app_.playbackStatus();
    if (playback_status == PlaybackStatus::Error &&
        frame_now_playing_ &&
        frame_now_playing_->track_info &&
        frame_now_playing_->track_info->is_stream) {
        if (!pending_error_advance_) {
            auto* playlist = playbackPlaylistDocument();
            if (playlist) {
                const auto next_index = playbackAdvanceIndex(*playlist);
                if (next_index) {
                    pending_error_advance_ = true;
                    openTrackAt(playlist->id, *next_index);
                }
            }
        }
    } else {
        pending_error_advance_ = false;
    }

    frame_now_playing_ = app_.currentNowPlaying();
    syncPendingEndOfTrackAdvanceWithNowPlaying();
    syncPlaylistCursorToNowPlaying(frame_now_playing_ ? frame_now_playing_->playlist_tab_id : 0,
                                   frame_now_playing_ ? frame_now_playing_->playlist_item_id : 0);
    frame_snapshot_ = captureRenderSnapshot();
    const MprisSnapshot mpris_snapshot = captureMprisSnapshot();
    mpris_.publishSnapshot(mpris_snapshot);

    if (pending_mpris_seek_target_us_ &&
        !pending_mpris_seek_track_id_.empty() &&
        mpris_snapshot.track.track_id == pending_mpris_seek_track_id_ &&
        mpris_snapshot.playback_status != PlaybackStatus::Seeking &&
        std::llabs(mpris_snapshot.position_us - *pending_mpris_seek_target_us_) <= 1'000'000) {
        mpris_.notifySeeked(mpris_snapshot.position_us);
        pending_mpris_seek_target_us_.reset();
        pending_mpris_seek_track_id_.clear();
    }

    if (!shouldRenderFrame(minimized))
        return true;

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    processPendingDialogResults();
    importPendingExternalDrops();
    renderLayout();
    renderRenamePlaylistPopup();
    renderAddUrlPopup();
    renderKeyboardShortcutsPopup();
    renderAboutPopup();
    handleKeyboardShortcuts();

    ImGui::Render();
    SDL_SetRenderDrawColor(renderer_, 20, 20, 20, 255);
    SDL_RenderClear(renderer_);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    SDL_RenderPresent(renderer_);
    last_rendered_snapshot_ = frame_snapshot_;
    last_render_at_ = std::chrono::steady_clock::now();
    has_rendered_frame_ = true;
    redraw_requested_ = false;

    return true;
}

int MainWindow::nextWaitTimeoutMs(bool minimized) {
    using namespace std::chrono;

    if (redraw_requested_ || !has_rendered_frame_) {
        frame_idling_.is_idling = false;
        return 0;
    }

    const auto refresh_interval = targetRenderInterval(minimized);
    const auto now = steady_clock::now();
    const auto next_refresh = last_render_at_ + refresh_interval;
    if (next_refresh <= now) {
        frame_idling_.is_idling = false;
        return 0;
    }

    frame_idling_.is_idling = true;
    return static_cast<int>(
        duration_cast<milliseconds>(next_refresh - now).count());
}

std::chrono::milliseconds MainWindow::targetRenderInterval(bool minimized) const {
    using namespace std::chrono;

    if (minimized)
        return frame_idling_.minimized_render_interval;

    // After any event, keep a short burst of higher-frequency redraws so input
    // still feels responsive, then fall back to a lower idle refresh rate.
    const bool recent_input =
        frame_idling_.last_event_at != steady_clock::time_point{} &&
        (steady_clock::now() - frame_idling_.last_event_at) < frame_idling_.interaction_boost_window;
    if (recent_input)
        return frame_idling_.interactive_render_interval;

    const PlaybackStatus playback_status = app_.playbackStatus();
    const bool active_playback = playback_status == PlaybackStatus::Buffering ||
                                 playback_status == PlaybackStatus::Playing ||
                                 playback_status == PlaybackStatus::EndOfTrack;

    if (active_playback)
        return frame_idling_.playback_idle_render_interval;

    return frame_idling_.idle_render_interval;
}

bool MainWindow::processEvent(const SDL_Event& ev) {
    ImGui_ImplSDL3_ProcessEvent(&ev);
    redraw_requested_ = true;
    frame_idling_.last_event_at = std::chrono::steady_clock::now();

    if (ev.type == SDL_EVENT_QUIT)
        return false;
    if (ev.type == SDL_EVENT_KEY_DOWN &&
        ev.key.key == SDLK_Q &&
        (ev.key.mod & SDL_KMOD_CTRL)) {
        return false;
    }

    if (ev.type == SDL_EVENT_DROP_BEGIN) {
        pending_external_drop_paths_.clear();
    } else if (ev.type == SDL_EVENT_DROP_FILE && ev.drop.data) {
        pending_external_drop_paths_.emplace_back(ev.drop.data);
    }

    return true;
}

bool MainWindow::pumpEvents(int wait_timeout_ms) {
    SDL_Event ev;
    if (wait_timeout_ms < 0) {
        if (SDL_WaitEvent(&ev)) {
            if (!processEvent(ev))
                return false;
        }
    } else if (wait_timeout_ms > 0 && SDL_WaitEventTimeout(&ev, wait_timeout_ms)) {
        if (!processEvent(ev))
            return false;
    }

    while (SDL_PollEvent(&ev)) {
        if (!processEvent(ev))
            return false;
    }

    return true;
}

bool MainWindow::isWindowMinimized() const {
    return (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0;
}

bool MainWindow::shouldRenderFrame(bool minimized) const {
    if (redraw_requested_ || !has_rendered_frame_)
        return true;

    const auto now = std::chrono::steady_clock::now();
    const bool heartbeat_due = (now - last_render_at_) >= targetRenderInterval(minimized);
    return heartbeat_due || !(frame_snapshot_ == last_rendered_snapshot_);
}

MainWindow::RenderSnapshot MainWindow::captureRenderSnapshot() const {
    const auto info = frame_now_playing_ ? frame_now_playing_->track_info : nullptr;
    const double position_seconds = app_.positionSeconds();
    const double duration_seconds = app_.durationSeconds();
    const int64_t instantaneous_bitrate_bps = app_.instantaneousBitrateBps();
    const size_t ring_capacity_samples = app_.ringCapacitySamples();
    const size_t ring_read = app_.ringReadPosition() % std::max<size_t>(ring_capacity_samples, 1);
    const size_t ring_write = app_.ringWritePosition() % std::max<size_t>(ring_capacity_samples, 1);

    return RenderSnapshot{
        .playback_status = app_.playbackStatus(),
        .now_playing_item_id = frame_now_playing_ ? frame_now_playing_->playlist_item_id : 0,
        .track_info = info,
        .position_ticks = static_cast<int64_t>(position_seconds * 20.0),
        .duration_ticks = static_cast<int64_t>(duration_seconds * 20.0),
        .bitrate_kbps = instantaneous_bitrate_bps > 0 ? instantaneous_bitrate_bps / 1000 : 0,
        .ring_read_bucket = ring_read / 1024,
        .ring_write_bucket = ring_write / 1024,
        .volume_centidb = static_cast<int>(std::lround(linearGainToDb(app_.volume()) * 100.0f)),
    };
}

bool MainWindow::RenderSnapshot::operator==(const RenderSnapshot& other) const {
    return playback_status == other.playback_status &&
           now_playing_item_id == other.now_playing_item_id &&
           sameSharedOwner(track_info, other.track_info) &&
           position_ticks == other.position_ticks &&
           duration_ticks == other.duration_ticks &&
           bitrate_kbps == other.bitrate_kbps &&
           ring_read_bucket == other.ring_read_bucket &&
           ring_write_bucket == other.ring_write_bucket &&
           volume_centidb == other.volume_centidb;
}

void MainWindow::renderLayout() {
    renderDockspaceHost();

    if (show_browser_window_) renderBrowserWindow();
    if (show_playlist_window_) renderPlaylistWindow();
    if (show_transport_window_) renderTransportWindow();
    if (show_inspector_window_) renderInspectorWindow();
    if (show_lyrics_window_) renderLyricsWindow();
    if (show_album_art_window_) renderAlbumArtWindow();
    if (show_settings_window_) renderSettingsWindow();
}

void MainWindow::renderDockspaceHost() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking |
                                    ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus |
                                    ImGuiWindowFlags_NoNavFocus |
                                    ImGuiWindowFlags_MenuBar;

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("ImpulseDockSpace", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    renderMainMenu();

    const ImGuiID dockspace_id = ImGui::GetID("ImpulseDockspace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    if (should_build_default_layout_ || request_layout_reset_) {
        buildDefaultDockLayout(dockspace_id);
    } else {
        dock_layout_built_ = true;
    }

    ImGui::End();
}

void MainWindow::buildDefaultDockLayout(ImGuiID dockspace_id) {
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

    // Docked windows do not auto-size to content, so choose a tighter default height
    // for the transport strip based on a target pixel height instead of a large ratio.
    const float top_ratio = std::clamp(kTransportPanelMinHeight / viewport->Size.y, 0.10f, 0.18f);

    ImGuiID lower_id = dockspace_id;
    const ImGuiID top_id = ImGui::DockBuilderSplitNode(lower_id, ImGuiDir_Up, top_ratio, nullptr, &lower_id);

    ImGuiID center_id = lower_id;
    const ImGuiID left_id = ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Left, 0.20f, nullptr, &center_id);
    const ImGuiID right_id = ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Right, 0.22f, nullptr, &center_id);
    ImGuiID lyrics_id = center_id;
    const ImGuiID playlists_id = ImGui::DockBuilderSplitNode(lyrics_id, ImGuiDir_Up, 0.54f, nullptr, &lyrics_id);

    ImGui::DockBuilderDockWindow("Browser", left_id);
    ImGui::DockBuilderDockWindow("Playlists", playlists_id);
    ImGui::DockBuilderDockWindow("Transport", top_id);
    ImGui::DockBuilderDockWindow("Inspector", right_id);
    ImGui::DockBuilderDockWindow("Lyrics", lyrics_id);
    ImGui::DockBuilderFinish(dockspace_id);

    dock_layout_built_ = true;
    request_layout_reset_ = false;
    should_build_default_layout_ = false;
}

void MainWindow::renderMainMenu() {
    if (!ImGui::BeginMenuBar())
        return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Playlist", "Ctrl+N")) {
            requestNewPlaylist();
        }
        if (ImGui::MenuItem("Open Playlist...", "Ctrl+O")) {
            requestOpenPlaylistDialog();
        }
        if (ImGui::MenuItem("Add URL...", "Ctrl+L")) {
            requestAddUrl();
        }
        if (ImGui::MenuItem("Save Playlist...", "Ctrl+S")) {
            requestSavePlaylistDialog(workspace_.activePlaylistId());
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Settings...", "Ctrl+,")) {
            syncSettingsFormFromCurrent();
            show_settings_window_ = true;
        }
        if (ImGui::MenuItem("Save Session")) {
            saveSession();
            status_message_ = "Session saved.";
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q"))
            requestQuit();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        const auto selected = selectedPlaylistIndices();
        if (ImGui::MenuItem("Select All", "Ctrl+A", false, !activePlaylist().empty())) {
            clearPlaylistSelection();
            auto& view_state = playlist_view_states_[workspace_.activePlaylistId()];
            for (const auto& track : activePlaylist().tracks())
                view_state.selection.insert(track.id);
        }
        if (ImGui::MenuItem("Remove Selected", "Delete", false, !selected.empty()))
            removeSelectedPlaylistItems();
        if (ImGui::MenuItem("Clear Playlist", nullptr, false, !activePlaylist().empty())) {
            activePlaylist().clear();
            clearPlaylistSelection();
            notifyPlaylistStructureChanged();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Playback")) {
        const bool can_seek = frame_now_playing_ &&
            frame_now_playing_->track_info &&
            frame_now_playing_->track_info->seekable &&
            app_.durationSeconds() > 0.0;
        const bool has_playback_context =
            playbackPlaylistDocument() != nullptr || !activePlaylist().empty();
        if (ImGui::MenuItem("Play/Pause", "Space"))
            togglePlayPause();
        if (ImGui::MenuItem("Stop", nullptr, false, app_.playbackStatus() != PlaybackStatus::Stopped))
            app_.commandStop();
        if (ImGui::MenuItem("Previous Track", "Ctrl+Left", false, has_playback_context))
            openPrevTrack();
        if (ImGui::MenuItem("Next Track", "Ctrl+Right", false, has_playback_context))
            openNextTrack();
        ImGui::Separator();
        if (ImGui::MenuItem("Seek Backward 5s", "Alt+Left", false, can_seek))
            seekRelative(-5.0);
        if (ImGui::MenuItem("Seek Forward 5s", "Alt+Right", false, can_seek))
            seekRelative(5.0);
        ImGui::Separator();
        if (ImGui::MenuItem("Volume Up", "Ctrl+Up"))
            adjustVolume(0.05f);
        if (ImGui::MenuItem("Volume Down", "Ctrl+Down"))
            adjustVolume(-0.05f);
        ImGui::Separator();
        if (ImGui::BeginMenu("Repeat")) {
            if (ImGui::MenuItem("Off", "R", settings_.repeat_mode == RepeatMode::Off))
                applyRepeatMode(RepeatMode::Off);
            if (ImGui::MenuItem("Playlist", nullptr, settings_.repeat_mode == RepeatMode::Playlist))
                applyRepeatMode(RepeatMode::Playlist);
            if (ImGui::MenuItem("Track", nullptr, settings_.repeat_mode == RepeatMode::Track))
                applyRepeatMode(RepeatMode::Track);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Browser", nullptr, &show_browser_window_);
        ImGui::MenuItem("Playlists", nullptr, &show_playlist_window_);
        ImGui::MenuItem("Transport", nullptr, &show_transport_window_);
        ImGui::MenuItem("Inspector", nullptr, &show_inspector_window_);
        ImGui::MenuItem("Lyrics", nullptr, &show_lyrics_window_);
        ImGui::MenuItem("Album Art", nullptr, &show_album_art_window_);
        if (ImGui::MenuItem("Reset Dock Layout")) {
            request_layout_reset_ = true;
            status_message_ = "Dock layout reset.";
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Playlist")) {
        const auto selected = selectedPlaylistIndices();
        if (ImGui::MenuItem("Play Selected", "Enter", false, !selected.empty())) {
            if (auto selected = selectedPlaylistIndices(); !selected.empty())
                openTrackAt(selected.front());
        }
        if (ImGui::MenuItem("Shuffle Playlist", "Ctrl+Shift+S", false, activePlaylist().size() > 1))
            shufflePlaylist(workspace_.activePlaylistId());
        ImGui::Separator();
        if (ImGui::MenuItem("Rename Playlist")) {
            beginRenamePlaylist(workspace_.activePlaylistId());
        }
        if (ImGui::MenuItem("Close Playlist", "Ctrl+W", false, workspace_.playlists().size() > 1)) {
            requestClosePlaylist(workspace_.activePlaylistId());
        }
        if (ImGui::MenuItem("Clear Playlist", nullptr, false, !activePlaylist().empty())) {
            activePlaylist().clear();
            clearPlaylistSelection();
            notifyPlaylistStructureChanged();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Keyboard Shortcuts", "F1"))
            open_shortcuts_popup_ = true;
        if (ImGui::MenuItem("About Impulse"))
            open_about_popup_ = true;
        ImGui::EndMenu();
    }

    if (!status_message_.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", status_message_.c_str());
    }

    ImGui::EndMenuBar();
}

std::vector<const DirEntry*> MainWindow::visibleBrowserEntries() const {
    std::vector<const DirEntry*> entries;
    entries.reserve(browser_.entries().size());
    for (const auto& entry : browser_.entries())
        entries.push_back(&entry);
    return entries;
}

void MainWindow::renderBrowserWindow() {
    if (!ImGui::Begin("Browser", &show_browser_window_)) {
        ImGui::End();
        return;
    }

    const bool at_filesystem_root = browser_.atFilesystemRoot();

    if (ImGui::Button("Root")) {
        browser_.navigate(browser_.rootPath());
        browser_selected_path_ = browser_.currentPath();
    }
    ImGui::SameLine();
    if (ImGui::Button("Home")) {
        browser_.navigate(defaultHomePath());
        browser_selected_path_ = browser_.currentPath();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(at_filesystem_root);
    if (ImGui::Button("Up") && !at_filesystem_root) {
        browser_.navigateUp();
        browser_selected_path_ = browser_.currentPath();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
        browser_.refreshCurrent();

    ImGui::SameLine(0.0f, 18.0f);
    const std::string current_path = browser_.currentPath().string();
    ImGui::TextDisabled("%s", current_path.c_str());
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", current_path.c_str());

    std::optional<std::filesystem::path> navigate_to;

    if (ImGui::BeginTable("browser_table", 1,
                          ImGuiTableFlags_Borders |
                          ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable |
                          ImGuiTableFlags_Sortable |
                          ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_SizingStretchProp,
                          ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort, 1.0f, kBrowserSortName);
        ImGui::TableHeadersRow();

        auto entries = visibleBrowserEntries();
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs();
            sort_specs && sort_specs->SpecsCount > 0) {
            const ImGuiTableColumnSortSpecs spec = sort_specs->Specs[0];
            std::ranges::sort(entries, [&](const DirEntry* lhs, const DirEntry* rhs) {
                if (lhs->is_directory != rhs->is_directory)
                    return lhs->is_directory > rhs->is_directory;

                const bool descending = spec.SortDirection == ImGuiSortDirection_Descending;
                auto less = [&](auto&& a, auto&& b) {
                    if (descending) return b < a;
                    return a < b;
                };

                return less(toLower(lhs->display_name), toLower(rhs->display_name));
            });
        }

        for (const DirEntry* entry : entries) {
            ImGui::PushID(entry->path.string().c_str());
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            const bool selected = browser_selected_path_ == entry->path;
            const bool clicked = ImGui::Selectable("##browser_row", selected,
                                                   ImGuiSelectableFlags_SpanAllColumns |
                                                   ImGuiSelectableFlags_AllowDoubleClick);
            if (clicked)
                browser_selected_path_ = entry->path;

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (entry->is_directory) {
                    navigate_to = entry->path;
                } else {
                    addTrackToPlaylist(entry->path, false, false);
                }
            }

            if (ImGui::BeginPopupContextItem("browser_row_context")) {
                if (entry->is_directory) {
                    if (ImGui::MenuItem("Open"))
                        navigate_to = entry->path;
                    if (ImGui::MenuItem("Add Folder to Playlist"))
                        addDirectoryToPlaylist(entry->path, false, false, false);
                    if (ImGui::MenuItem("Replace Playlist with Folder"))
                        replacePlaylistWithPaths(FileBrowser::collectAudioFiles(entry->path, false), false);
                    if (ImGui::MenuItem("Add Folder Tree"))
                        addDirectoryToPlaylist(entry->path, true, false, false);
                    if (ImGui::MenuItem("Replace Playlist with Folder Tree"))
                        replacePlaylistWithPaths(FileBrowser::collectAudioFiles(entry->path, true), false);
                    if (ImGui::MenuItem("Play Folder Tree"))
                        addDirectoryToPlaylist(entry->path, true, true, false);
                } else {
                    if (ImGui::MenuItem("Play Now"))
                        addTrackToPlaylist(entry->path, true, false);
                    if (ImGui::MenuItem("Add to Playlist"))
                        addTrackToPlaylist(entry->path, false, false);
                    if (ImGui::MenuItem("Replace Playlist"))
                        replacePlaylistWithPaths({entry->path}, false);
                    if (ImGui::MenuItem("Insert Next"))
                        addTrackToPlaylist(entry->path, false, true);
                }
                ImGui::EndPopup();
            }

            if (ImGui::BeginDragDropSource()) {
                drag_browser_paths_.clear();
                if (entry->is_directory)
                    drag_browser_paths_ = FileBrowser::collectAudioFiles(entry->path, true);
                else
                    drag_browser_paths_ = {entry->path};
                ImGui::SetDragDropPayload("IMPULSE_BROWSER_PATHS", "browser", sizeof("browser"));
                ImGui::Text("%zu track(s)", drag_browser_paths_.size());
                ImGui::TextUnformatted(entry->display_name.c_str());
                ImGui::EndDragDropSource();
            }

            ImGui::SameLine();
            ImGui::TextUnformatted(entry->display_name.c_str());
            ImGui::PopID();
        }

        if (ImGui::BeginPopupContextWindow("browser_empty_context",
                                           ImGuiPopupFlags_MouseButtonRight |
                                           ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Refresh"))
                browser_.refreshCurrent();
            if (ImGui::MenuItem("Add Current Folder"))
                addDirectoryToPlaylist(browser_.currentPath(), false, false, false);
            if (ImGui::MenuItem("Replace Playlist with Current Folder"))
                replacePlaylistWithPaths(FileBrowser::collectAudioFiles(browser_.currentPath(), false), false);
            if (ImGui::MenuItem("Add Current Folder Tree"))
                addDirectoryToPlaylist(browser_.currentPath(), true, false, false);
            if (ImGui::MenuItem("Replace Playlist with Current Folder Tree"))
                replacePlaylistWithPaths(FileBrowser::collectAudioFiles(browser_.currentPath(), true), false);
            ImGui::EndPopup();
        }

        ImGui::EndTable();
    }

    if (navigate_to) {
        browser_.navigate(*navigate_to);
        browser_selected_path_ = browser_.currentPath();
    }

    ImGui::End();
}

void MainWindow::applyPlaylistSortFromTable(ImGuiTableSortSpecs* sort_specs) {
    if (!sort_specs || sort_specs->SpecsCount == 0 || !sort_specs->SpecsDirty)
        return;

    const ImGuiTableColumnSortSpecs spec = sort_specs->Specs[0];
    const auto key = static_cast<PlaylistSortKey>(spec.ColumnUserID);
    activePlaylist().sortBy(key, sortDirectionFromImGui(spec.SortDirection));
    notifyPlaylistStructureChanged();
    sort_specs->SpecsDirty = false;
}

void MainWindow::renderPlaylistWindow() {
    if (!ImGui::Begin("Playlists", &show_playlist_window_)) {
        ImGui::End();
        return;
    }

    const uint64_t playing_playlist_id = playbackPlaylistId();
    if (ImGui::BeginTabBar("playlist_tabs", ImGuiTabBarFlags_Reorderable)) {
        std::vector<uint64_t> close_requests;
        for (const auto& playlist : workspace_.playlists()) {
            bool open = true;
            const bool is_playing = playlist.id == playing_playlist_id;
            const ImGuiTabItemFlags flags = playlist.id == pending_focus_playlist_id_
                ? ImGuiTabItemFlags_SetSelected
                : ImGuiTabItemFlags_None;

            if (ImGui::BeginTabItem(playlistTabLabel(playlist, is_playing).c_str(), &open, flags)) {
                workspace_.activatePlaylist(playlist.id);
                if (pending_focus_playlist_id_ == playlist.id)
                    pending_focus_playlist_id_ = 0;
                ImGui::EndTabItem();
            }

            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Rename"))
                    beginRenamePlaylist(playlist.id);
                if (ImGui::MenuItem("Save..."))
                    requestSavePlaylistDialog(playlist.id);
                if (ImGui::MenuItem("Close", nullptr, false, workspace_.playlists().size() > 1))
                    close_requests.push_back(playlist.id);
                ImGui::EndPopup();
            }

            if (!open)
                close_requests.push_back(playlist.id);
        }

        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing))
            requestNewPlaylist();

        ImGui::EndTabBar();

        std::ranges::sort(close_requests);
        close_requests.erase(std::unique(close_requests.begin(), close_requests.end()), close_requests.end());
        for (uint64_t playlist_id : close_requests)
            closePlaylist(playlist_id);
    }

    PlaylistManager& playlist = activePlaylist();
    const auto selected = selectedPlaylistIndices();

    if (ImGui::Button("Play") && !selected.empty()) {
        if (auto selected_indices = selectedPlaylistIndices(); !selected_indices.empty())
            openTrackAt(selected_indices.front());
    }
    ImGui::SameLine();
    if (ImGui::Button("Shuffle") && playlist.size() > 1)
        shufflePlaylist(workspace_.activePlaylistId());
    ImGui::SameLine();
    if (ImGui::Button("Remove") && !selected.empty())
        removeSelectedPlaylistItems();
    ImGui::SameLine();
    if (ImGui::Button("Rename"))
        beginRenamePlaylist(workspace_.activePlaylistId());
    ImGui::SameLine();
    if (ImGui::Button("Save..."))
        requestSavePlaylistDialog(workspace_.activePlaylistId());
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        playlist.clear();
        clearPlaylistSelection();
        notifyPlaylistStructureChanged();
    }
    ImGui::SameLine();
    if (selected.empty())
        ImGui::TextDisabled("%zu track(s)", playlist.size());
    else
        ImGui::TextDisabled("%zu track(s) | %zu selected", playlist.size(), selected.size());

    std::optional<size_t> play_index;
    std::vector<size_t> remove_indices;
    std::optional<std::pair<std::vector<size_t>, size_t>> move_request;
    bool append_browser_drop = false;
    const auto table_id = std::format("playlist_table_{}", workspace_.activePlaylistId());

    if (ImGui::BeginTable(table_id.c_str(), 9,
                          ImGuiTableFlags_Borders |
                          ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable |
                          ImGuiTableFlags_Reorderable |
                          ImGuiTableFlags_Hideable |
                          ImGuiTableFlags_Sortable |
                          ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_SizingFixedFit,
                          ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn(" ", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed,
                                26.0f,
                                static_cast<ImGuiID>(PlaylistSortKey::Order));
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed,
                                44.0f, static_cast<ImGuiID>(PlaylistSortKey::TrackNumber));
        ImGui::TableSetupColumn("Album", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultHide, 0.40f,
                                static_cast<ImGuiID>(PlaylistSortKey::Album));
        ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch, 0.60f,
                                static_cast<ImGuiID>(PlaylistSortKey::Title));
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed,
                                72.0f, static_cast<ImGuiID>(PlaylistSortKey::Duration));
        ImGui::TableSetupColumn("Codec", ImGuiTableColumnFlags_WidthFixed, 74.0f,
                                static_cast<ImGuiID>(PlaylistSortKey::Codec));
        ImGui::TableSetupColumn("BR", ImGuiTableColumnFlags_WidthFixed, 56.0f,
                                static_cast<ImGuiID>(PlaylistSortKey::Bitrate));
        ImGui::TableSetupColumn("RG", ImGuiTableColumnFlags_WidthFixed, 58.0f,
                                static_cast<ImGuiID>(PlaylistSortKey::ReplayGain));
        ImGui::TableSetupColumn("PLR", ImGuiTableColumnFlags_WidthFixed, 58.0f,
                                static_cast<ImGuiID>(PlaylistSortKey::PLR));
        ImGui::TableHeadersRow();

        applyPlaylistSortFromTable(ImGui::TableGetSortSpecs());

        for (size_t i = 0; i < playlist.size(); ++i) {
            const PlaylistItem& item = playlist.tracks()[i];
            ImGui::PushID(static_cast<int>(item.id));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            const bool row_selected = isPlaylistSelected(item.id);
            const bool clicked = ImGui::Selectable("##playlist_row", row_selected,
                                                   ImGuiSelectableFlags_SpanAllColumns |
                                                   ImGuiSelectableFlags_AllowDoubleClick);
            if (clicked)
                selectPlaylistIndex(i, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                play_index = i;

            if (ImGui::BeginPopupContextItem("playlist_row_context")) {
                if (ImGui::MenuItem("Play"))
                    play_index = i;
                if (ImGui::MenuItem("Remove"))
                    remove_indices.push_back(i);
                if (ImGui::MenuItem("Move to Top"))
                    move_request = std::pair{std::vector<size_t>{i}, size_t{0}};
                if (ImGui::MenuItem("Move to Bottom"))
                    move_request = std::pair{std::vector<size_t>{i}, playlist.size()};
                ImGui::EndPopup();
            }

            if (ImGui::BeginDragDropSource()) {
                if (!row_selected) {
                    clearPlaylistSelection();
                    playlist_view_states_[workspace_.activePlaylistId()].selection.insert(item.id);
                }
                drag_playlist_ids_.clear();
                for (size_t index : selectedPlaylistIndices())
                    drag_playlist_ids_.push_back(playlist.tracks()[index].id);
                ImGui::SetDragDropPayload("IMPULSE_PLAYLIST_ROWS",
                                          drag_playlist_ids_.data(),
                                          drag_playlist_ids_.size() * sizeof(uint64_t));
                ImGui::Text("%zu track(s)", drag_playlist_ids_.size());
                ImGui::TextUnformatted(item.displayTitle().c_str());
                ImGui::EndDragDropSource();
            }

            if (ImGui::BeginDragDropTarget()) {
                if (ImGui::AcceptDragDropPayload("IMPULSE_PLAYLIST_ROWS")) {
                    std::vector<size_t> indices;
                    for (uint64_t id : drag_playlist_ids_) {
                        if (auto index = playlist.indexOf(id))
                            indices.push_back(*index);
                    }
                    const bool insert_after = ImGui::GetMousePos().y >
                        (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f;
                    move_request = std::pair{indices, i + (insert_after ? 1 : 0)};
                }
                if (ImGui::AcceptDragDropPayload("IMPULSE_BROWSER_PATHS"))
                    append_browser_drop = true;
                ImGui::EndDragDropTarget();
            }

            ImGui::SameLine();
            const bool is_now_playing = frame_now_playing_ &&
                frame_now_playing_->playlist_tab_id == workspace_.activePlaylistId() &&
                item.id == frame_now_playing_->playlist_item_id;
            ImGui::TextUnformatted(is_now_playing ? ">" : "");

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(item.track_number.empty() ? "--" : item.track_number.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(item.album.empty() ? "--" : item.album.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(item.displayTitle().c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(item.duration_seconds > 0.0 ? formatTime(item.duration_seconds).c_str() : "--");
            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(item.codec.empty() ? "--" : item.codec.c_str());
            ImGui::TableSetColumnIndex(6);
            ImGui::TextUnformatted(formatKbpsValue(item.bitrate_bps).c_str());
            ImGui::TableSetColumnIndex(7);
            const ReplayGain::GainApplication rg_application =
                replayGainApplicationForItem(item, settings_);
            const bool replaygain_would_clip =
                ReplayGain::wouldClip(rg_application).value_or(false);
            if (replaygain_would_clip)
                ImGui::PushStyleColor(ImGuiCol_Text, kReplayGainClipTextColor);
            ImGui::Text("%.1f", rg_application.applied_gain_db);
            if (replaygain_would_clip)
                ImGui::PopStyleColor();
            ImGui::TableSetColumnIndex(8);
            if (item.plr_lu)
                ImGui::Text("%.1f", *item.plr_lu);
            else
                ImGui::TextUnformatted("--");

            ImGui::PopID();
        }

        if (ImGui::BeginPopupContextWindow("playlist_empty_context",
                                           ImGuiPopupFlags_MouseButtonRight |
                                           ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Remove Selected", nullptr, false, !selected.empty()))
                removeSelectedPlaylistItems();
            if (ImGui::MenuItem("Clear Playlist", nullptr, false, !playlist.empty())) {
                playlist.clear();
                clearPlaylistSelection();
                notifyPlaylistStructureChanged();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginDragDropTarget()) {
            if (ImGui::AcceptDragDropPayload("IMPULSE_BROWSER_PATHS"))
                append_browser_drop = true;
            if (ImGui::AcceptDragDropPayload("IMPULSE_PLAYLIST_ROWS")) {
                std::vector<size_t> indices;
                for (uint64_t id : drag_playlist_ids_) {
                    if (auto index = playlist.indexOf(id))
                        indices.push_back(*index);
                }
                move_request = std::pair{indices, playlist.size()};
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::EndTable();
    }

    if (!remove_indices.empty()) {
        playlist.removeTracks(remove_indices);
        notifyPlaylistStructureChanged();
    }
    if (move_request) {
        playlist.moveTracks(move_request->first, move_request->second);
        notifyPlaylistStructureChanged();
    }
    if (append_browser_drop)
        addPathsToPlaylist(drag_browser_paths_, false, false);
    if (play_index)
        openTrackAt(*play_index);

    ImGui::End();
}

void MainWindow::renderTransportWindow() {
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(0.0f, kTransportPanelMinHeight),
        ImVec2(std::numeric_limits<float>::max(), std::numeric_limits<float>::max()));
    if (!ImGui::Begin("Transport", &show_transport_window_)) {
        ImGui::End();
        return;
    }

    auto info = frame_now_playing_ ? frame_now_playing_->track_info : nullptr;
    auto status = app_.playbackStatus();
    double pos  = app_.positionSeconds();
    double dur  = app_.durationSeconds();
    float volume_linear = std::clamp(app_.volume(), 0.0f, 1.0f);
    const int64_t instant_bitrate_bps = app_.instantaneousBitrateBps();
    const size_t buffered_samples = app_.bufferedSamples();
    const size_t ring_capacity_samples = app_.ringCapacitySamples();
    const size_t ring_read = app_.ringReadPosition() % std::max<size_t>(ring_capacity_samples, 1);
    const size_t ring_write = app_.ringWritePosition() % std::max<size_t>(ring_capacity_samples, 1);
    const float current_kbps = instant_bitrate_bps > 0
        ? static_cast<float>(instant_bitrate_bps) / 1000.0f
        : 0.0f;
    if (!sameSharedOwner(info, bitrate_peak_track_)) {
        bitrate_peak_track_ = info;
        bitrate_bar_peak_kbps_ = 0.0f;
    }
    if (!info)
        bitrate_bar_peak_kbps_ = 0.0f;
    if (current_kbps > bitrate_bar_peak_kbps_)
        bitrate_bar_peak_kbps_ = current_kbps;

    const float displayed_peak_kbps = bitrate_bar_peak_kbps_ > 0.0f
        ? bitrate_bar_peak_kbps_
        : 1.0f;

    const bool can_seek = info && info->seekable && dur > 0.0;
    const float live_progress = can_seek ? static_cast<float>(pos / dur) : 0.0f;
    if (!can_seek)
        seek_drag_progress_.reset();
    float progress = seek_drag_progress_.value_or(live_progress);
    if (ImGui::BeginTable("transport_layout", 2,
                          ImGuiTableFlags_SizingStretchProp,
                          ImVec2(-1.0f, 0.0f))) {
        ImGui::TableSetupColumn("playback", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("metrics", ImGuiTableColumnFlags_WidthStretch, 1.0f);

        ImGui::TableNextColumn();
        if (info) {
            const std::string track_title = info->title.empty()
                ? info->source.displayName()
                : info->title;
            ImGui::TextUnformatted(track_title.c_str());
            if (!info->artist.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("- %s", info->artist.c_str());
            }
            if (const auto* playback_playlist = playbackPlaylistDocument()) {
                ImGui::TextDisabled("Playlist: %s", playback_playlist->name.c_str());
                if (playback_playlist->id != workspace_.activePlaylistId())
                    ImGui::TextDisabled("Viewing: %s", activePlaylistDocument().name.c_str());
            }
        } else {
            ImGui::TextDisabled("Drop files, add a folder, or double-click a track to start.");
        }

        if (ImGui::BeginTable("transport_controls", 3,
                              ImGuiTableFlags_SizingStretchProp,
                              ImVec2(-1.0f, 0.0f))) {
            ImGui::TableSetupColumn("buttons", ImGuiTableColumnFlags_WidthFixed, 0.0f);
            ImGui::TableSetupColumn("volume", ImGuiTableColumnFlags_WidthFixed, 220.0f);
            ImGui::TableSetupColumn("progress", ImGuiTableColumnFlags_WidthStretch, 1.0f);

            ImGui::TableNextColumn();
            if (ImGui::Button("Prev")) openPrevTrack();
            ImGui::SameLine();
            if (status == PlaybackStatus::Playing || status == PlaybackStatus::Buffering) {
                if (ImGui::Button("Pause")) togglePlayPause();
            } else {
                if (ImGui::Button("Play")) togglePlayPause();
            }
            ImGui::SameLine();
            if (ImGui::Button("Next")) openNextTrack();
            ImGui::SameLine();
            if (ImGui::Button("Stop")) app_.commandStop();

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            const std::string volume_label = formatVolumeDbLabel(volume_linear);
            if (ImGui::SliderFloat("##volume_linear", &volume_linear, 0.0f, 1.0f, ""))
                app_.commandSetVolume(volume_linear);
            ImGui::RenderTextClipped(ImGui::GetItemRectMin(),
                                     ImGui::GetItemRectMax(),
                                     volume_label.c_str(),
                                     nullptr,
                                     nullptr,
                                     ImVec2(0.5f, 0.5f));

            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s  %s / %s",
                                playbackStatusLabel(status),
                                formatTime(pos).c_str(),
                                can_seek ? formatTime(dur).c_str() : "Live");

            ImGui::EndTable();
        }

        ImGui::TableNextColumn();
        if (ImGui::BeginTable("transport_metrics", 2,
                              ImGuiTableFlags_SizingStretchProp,
                              ImVec2(-1.0f, 0.0f))) {
            ImGui::TableSetupColumn("bitrate", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn("ring", ImGuiTableColumnFlags_WidthStretch, 1.2f);

            ImGui::TableNextColumn();
            const std::string bitrate_bar_label = std::format("{} / {:.0f} kbps",
                                                              formatKbpsValue(instant_bitrate_bps),
                                                              displayed_peak_kbps);
            ImGui::Text("Bitrate");
            ImGui::ProgressBar(displayed_peak_kbps > 0.0f
                                   ? std::clamp(current_kbps / displayed_peak_kbps, 0.0f, 1.0f)
                                   : 0.0f,
                               ImVec2(-1.0f, 0.0f),
                               bitrate_bar_label.c_str());

            ImGui::TableNextColumn();
            ImGui::Text("Ring Buffer");
            const ImVec2 ring_size(ImVec2(ImGui::GetContentRegionAvail().x, 18.0f));
            const ImVec2 ring_pos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##ring_visualization", ring_size);
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            const ImU32 bg_col = ImGui::GetColorU32(ImGuiCol_FrameBg);
            const ImU32 fill_col = ImGui::GetColorU32(ImVec4(0.18f, 0.62f, 0.36f, 1.0f));
            const ImU32 read_col = ImGui::GetColorU32(ImVec4(0.95f, 0.75f, 0.22f, 1.0f));
            const ImU32 write_col = ImGui::GetColorU32(ImVec4(0.34f, 0.68f, 0.96f, 1.0f));
            const float radius = 6.0f;
            draw_list->AddRectFilled(ring_pos, ImVec2(ring_pos.x + ring_size.x, ring_pos.y + ring_size.y), bg_col, radius);
            draw_list->AddRect(ring_pos, ImVec2(ring_pos.x + ring_size.x, ring_pos.y + ring_size.y),
                               ImGui::GetColorU32(ImGuiCol_Border), radius);

            if (ring_capacity_samples > 0 && buffered_samples > 0) {
                const float read_frac = static_cast<float>(ring_read) / static_cast<float>(ring_capacity_samples);
                const float write_frac = static_cast<float>(ring_write) / static_cast<float>(ring_capacity_samples);
                auto x_for = [&](float frac) {
                    return ring_pos.x + frac * ring_size.x;
                };

                if (ring_write >= ring_read) {
                    draw_list->AddRectFilled(ImVec2(x_for(read_frac), ring_pos.y),
                                             ImVec2(x_for(write_frac), ring_pos.y + ring_size.y),
                                             fill_col, radius);
                } else {
                    draw_list->AddRectFilled(ImVec2(ring_pos.x, ring_pos.y),
                                             ImVec2(x_for(write_frac), ring_pos.y + ring_size.y),
                                             fill_col, radius);
                    draw_list->AddRectFilled(ImVec2(x_for(read_frac), ring_pos.y),
                                             ImVec2(ring_pos.x + ring_size.x, ring_pos.y + ring_size.y),
                                             fill_col, radius);
                }

                draw_list->AddLine(ImVec2(x_for(read_frac), ring_pos.y),
                                   ImVec2(x_for(read_frac), ring_pos.y + ring_size.y),
                                   read_col, 2.0f);
                draw_list->AddLine(ImVec2(x_for(write_frac), ring_pos.y),
                                   ImVec2(x_for(write_frac), ring_pos.y + ring_size.y),
                                   write_col, 2.0f);
            }

            ImGui::EndTable();
        }

        ImGui::EndTable();
    }

    ImGui::SetNextItemWidth(-1.0f);
    if (can_seek) {
        if (ImGui::SliderFloat("##seek", &progress, 0.0f, 1.0f, "")) {
            seek_drag_progress_ = progress;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (seek_drag_progress_)
                seekTo(*seek_drag_progress_ * dur);
            seek_drag_progress_.reset();
        }
    } else {
        ImGui::BeginDisabled();
        ImGui::SliderFloat("##seek", &progress, 0.0f, 1.0f, "");
        ImGui::EndDisabled();
    }

    int repeat_mode = static_cast<int>(settings_.repeat_mode);
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("##repeat_mode", repeatModeLabel(settings_.repeat_mode))) {
        for (int i = 0; i < 3; ++i) {
            const auto mode = static_cast<RepeatMode>(i);
            const bool selected = repeat_mode == i;
            if (ImGui::Selectable(repeatModeLabel(mode), selected))
                repeat_mode = i;
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Repeat");
    if (repeat_mode != static_cast<int>(settings_.repeat_mode))
        applyRepeatMode(static_cast<RepeatMode>(repeat_mode));

    ImGui::SameLine(0.0f, 20.0f);
    int replaygain_mode = static_cast<int>(settings_.replaygain_mode);
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("##replaygain_mode", replayGainModeLabel(settings_.replaygain_mode))) {
        for (int i = 0; i < 3; ++i) {
            const auto mode = static_cast<ReplayGain::GainMode>(i);
            const bool selected = replaygain_mode == i;
            if (ImGui::Selectable(replayGainModeLabel(mode), selected))
                replaygain_mode = i;
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("ReplayGain source");
    if (replaygain_mode != static_cast<int>(settings_.replaygain_mode)) {
        settings_.replaygain_mode = static_cast<ReplayGain::GainMode>(replaygain_mode);
        app_.commandSetReplayGainSettings(settings_.replayGainSettings());
        if (!SettingsStorage::save(settings_path_, settings_))
            status_message_ = "ReplayGain mode changed for this session, but settings could not be saved.";
        redraw_requested_ = true;
    }

    ImGui::End();
}

void MainWindow::renderInspectorWindow() {
    if (!ImGui::Begin("Inspector", &show_inspector_window_)) {
        ImGui::End();
        return;
    }

    auto info = frame_now_playing_ ? frame_now_playing_->track_info : nullptr;

    if (ImGui::CollapsingHeader("Overview", ImGuiTreeNodeFlags_DefaultOpen))
        renderNowPlayingTab(info);
    if (ImGui::CollapsingHeader("Technical", ImGuiTreeNodeFlags_DefaultOpen))
        renderTechInfoPanel();
    if (ImGui::CollapsingHeader("Metadata", ImGuiTreeNodeFlags_DefaultOpen))
        renderMetadataPanel();

    ImGui::End();
}

void MainWindow::renderLyricsWindow() {
    if (!ImGui::Begin("Lyrics", &show_lyrics_window_)) {
        ImGui::End();
        return;
    }

    auto info = frame_now_playing_ ? frame_now_playing_->track_info : nullptr;
    syncLyricsDocument(info);

    if (!info) {
        ImGui::TextDisabled("No track loaded.");
        ImGui::End();
        return;
    }

    if (lyrics_document_.kind == LyricsKind::None) {
        if (info->source.isFile()) {
            ImGui::TextDisabled("No lyrics found in the LYRICS tag or %s.lrc.",
                                info->source.stem().c_str());
        } else {
            ImGui::TextDisabled("No lyrics found in the current track metadata.");
        }
        ImGui::End();
        return;
    }

    if (lyrics_document_.kind == LyricsKind::Untimed) {
        ImGui::BeginChild("lyrics_untimed_scroll", ImVec2(0.0f, 0.0f));
        ImGui::PushTextWrapPos(0.0f);
        for (const std::string& line : lyrics_document_.untimed_lines) {
            if (line.empty())
                ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
            else
                ImGui::TextWrapped("%s", line.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    const std::optional<size_t> active_index =
        activeTimedLyricLineIndex(lyrics_document_, app_.positionSeconds());
    if (active_index != last_active_timed_lyric_line_) {
        lyrics_should_autoscroll_ = true;
        last_active_timed_lyric_line_ = active_index;
    }

    ImGui::BeginChild("lyrics_timed_scroll", ImVec2(0.0f, 0.0f));
    if (!active_index && lyrics_should_autoscroll_) {
        ImGui::SetScrollY(0.0f);
        lyrics_should_autoscroll_ = false;
    }

    for (size_t i = 0; i < lyrics_document_.timed_lines.size(); ++i) {
        const TimedLyricLine& line = lyrics_document_.timed_lines[i];
        const bool is_current = active_index && *active_index == i;

        if (is_current) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.86f, 0.34f, 1.0f));
            ImGui::TextWrapped("%s", line.text.empty() ? " " : line.text.c_str());
            if (lyrics_should_autoscroll_) {
                ImGui::SetScrollHereY(0.5f);
                lyrics_should_autoscroll_ = false;
            }
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", line.text.empty() ? " " : line.text.c_str());
            ImGui::PopStyleColor();
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

void MainWindow::renderAlbumArtWindow() {
    ImGui::SetNextWindowSize(ImVec2(720.0f, 820.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Album Art", &show_album_art_window_)) {
        ImGui::End();
        return;
    }

    auto info = frame_now_playing_ ? frame_now_playing_->track_info : nullptr;
    syncAlbumArtTexture(info);

    if (!info || !album_art_texture_) {
        ImGui::TextDisabled("No album art available for the current track.");
        ImGui::End();
        return;
    }

    const std::string title = info->title.empty()
        ? info->source.displayName()
        : info->title;
    ImGui::TextWrapped("%s", title.c_str());
    if (!info->artist.empty())
        ImGui::TextDisabled("%s", info->artist.c_str());
    ImGui::TextDisabled("%d x %d", album_art_tex_w_, album_art_tex_h_);
    ImGui::Separator();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float max_w = std::max(avail.x, 1.0f);
    const float max_h = std::max(avail.y, 1.0f);
    const float width = static_cast<float>(album_art_tex_w_);
    const float height = static_cast<float>(album_art_tex_h_);
    const float scale = std::min({1.0f, max_w / width, max_h / height});
    const ImVec2 image_size(width * scale, height * scale);

    const float x = std::max((avail.x - image_size.x) * 0.5f, 0.0f);
    if (x > 0.0f)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x);
    ImGui::Image(reinterpret_cast<ImTextureID>(album_art_texture_), image_size);

    ImGui::End();
}

void MainWindow::syncSettingsFormFromCurrent() {
    copyToBuffer(settings_.browser_root_path.string(), settings_browser_root_buffer_);
    settings_buffer_ahead_seconds_ = settings_.buffer_ahead_seconds;
    settings_replaygain_with_rg_db_ = settings_.replaygain_preamp_with_rg_db;
    settings_replaygain_without_rg_db_ = settings_.replaygain_preamp_without_rg_db;
    settings_plr_reference_lufs_ = settings_.plr_reference_lufs;
}

bool MainWindow::applySettingsForm() {
    auto browser_root = canonicalDirectory(settings_browser_root_buffer_.data());
    if (!browser_root) {
        status_message_ = "Settings not saved: browser root must be an existing directory.";
        return false;
    }

    if (settings_plr_reference_lufs_ >= 0.0f) {
        status_message_ = "Settings not saved: PLR reference LUFS must be negative.";
        return false;
    }

    AppSettings updated = settings_;
    updated.browser_root_path = *browser_root;
    updated.buffer_ahead_seconds = sanitizeBufferAheadSeconds(settings_buffer_ahead_seconds_);
    updated.replaygain_preamp_with_rg_db = settings_replaygain_with_rg_db_;
    updated.replaygain_preamp_without_rg_db = settings_replaygain_without_rg_db_;
    updated.plr_reference_lufs = settings_plr_reference_lufs_;

    if (!SettingsStorage::save(settings_path_, updated)) {
        status_message_ = "Settings not saved: failed to write settings file.";
        return false;
    }

    settings_ = std::move(updated);
    browser_.setRoot(settings_.browser_root_path);
    browser_selected_path_ = browser_.currentPath();
    for (auto& playlist : workspace_.playlists())
        playlist.playlist.setPlrReferenceLufs(settings_.plr_reference_lufs);
    app_.commandSetBufferAheadSeconds(settings_.buffer_ahead_seconds);
    app_.commandSetReplayGainSettings(settings_.replayGainSettings());
    syncSettingsFormFromCurrent();
    show_settings_window_ = false;
    redraw_requested_ = true;
    status_message_ = "Settings saved.";
    return true;
}

void MainWindow::renderSettingsWindow() {
    if (show_settings_window_)
        ImGui::OpenPopup("Settings");

    bool keep_open = show_settings_window_;
    if (!ImGui::BeginPopupModal("Settings", &keep_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        show_settings_window_ = keep_open;
        return;
    }
    show_settings_window_ = keep_open;

    ImGui::TextDisabled("Browser");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("File browser root",
                             "Absolute directory path",
                             settings_browser_root_buffer_.data(),
                             settings_browser_root_buffer_.size());
    ImGui::TextDisabled("Home and Up stay inside this root.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("Playback");
    ImGui::SetNextItemWidth(220.0f);
    ImGui::SliderFloat("Buffer ahead (seconds)",
                       &settings_buffer_ahead_seconds_,
                       kMinBufferAheadSeconds,
                       kMaxBufferAheadSeconds,
                       "%.1f s");
    const double ring_capacity_seconds =
        static_cast<double>(app_.ringCapacitySamples()) /
        static_cast<double>(app_.outputSampleRate() * app_.outputChannels());
    ImGui::TextDisabled("Keeps decoded audio buffered ahead before FFmpeg idles. Ring capacity: %.1f s.",
                        ring_capacity_seconds);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("ReplayGain");
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputFloat("Tracks with ReplayGain (dB)", &settings_replaygain_with_rg_db_, 0.5f, 1.0f, "%.2f");
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputFloat("Tracks without ReplayGain (dB)", &settings_replaygain_without_rg_db_, 0.5f, 1.0f, "%.2f");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("PLR");
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputFloat("Reference LUFS", &settings_plr_reference_lufs_, 0.5f, 1.0f, "%.2f");
    ImGui::TextDisabled("Example: -18.0 for standard ReplayGain, -23.0 for EBU R128 workflows.");

    ImGui::Spacing();
    if (ImGui::Button("Save") && applySettingsForm())
        ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        syncSettingsFormFromCurrent();
        show_settings_window_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Defaults")) {
        const AppSettings defaults = defaultAppSettings();
        copyToBuffer(defaults.browser_root_path.string(), settings_browser_root_buffer_);
        settings_buffer_ahead_seconds_ = defaults.buffer_ahead_seconds;
        settings_replaygain_with_rg_db_ = defaults.replaygain_preamp_with_rg_db;
        settings_replaygain_without_rg_db_ = defaults.replaygain_preamp_without_rg_db;
        settings_plr_reference_lufs_ = defaults.plr_reference_lufs;
    }

    ImGui::EndPopup();
}

void MainWindow::renderKeyboardShortcutsPopup() {
    if (open_shortcuts_popup_) {
        ImGui::OpenPopup("Keyboard Shortcuts");
        open_shortcuts_popup_ = false;
    }

    if (!ImGui::BeginPopupModal("Keyboard Shortcuts", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextDisabled("General");
    ImGui::BulletText("Space: Play / Pause");
    ImGui::BulletText("Ctrl+Left / Ctrl+Right: Previous / Next track");
    ImGui::BulletText("Alt+Left / Alt+Right: Seek backward / forward 5 seconds");
    ImGui::BulletText("Ctrl+Up / Ctrl+Down: Volume up / down");
    ImGui::BulletText("R: Cycle repeat mode");
    ImGui::BulletText("F1: Show this shortcuts window");

    ImGui::Spacing();
    ImGui::TextDisabled("Playlist");
    ImGui::BulletText("Ctrl+N: New playlist");
    ImGui::BulletText("Ctrl+O: Open playlist file");
    ImGui::BulletText("Ctrl+S: Save active playlist");
    ImGui::BulletText("Ctrl+Shift+S: Shuffle active playlist");
    ImGui::BulletText("Ctrl+W: Close active playlist");
    ImGui::BulletText("Enter: Play selected track");
    ImGui::BulletText("Delete: Remove selected tracks");
    ImGui::BulletText("Ctrl+A: Select all tracks");

    ImGui::Spacing();
    ImGui::TextDisabled("App");
    ImGui::BulletText("Ctrl+,: Open settings");
    ImGui::BulletText("Ctrl+Q: Quit");

    ImGui::Spacing();
    if (ImGui::Button("Close"))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void MainWindow::renderAboutPopup() {
    if (open_about_popup_) {
        ImGui::OpenPopup("About Impulse");
        open_about_popup_ = false;
    }

    if (!ImGui::BeginPopupModal("About Impulse", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::Text("Impulse %s", kAppVersion.data());
    ImGui::Spacing();
    ImGui::TextWrapped("%s", kAppTagline.data());
    ImGui::Spacing();
    ImGui::TextDisabled("Built with SDL3, Dear ImGui, FFmpeg, and PipeWire.");
    ImGui::Separator();
    ImGui::TextDisabled("Licensing");
    ImGui::BulletText("Impulse: %s", kProjectLicenseName.data());
    ImGui::TextWrapped("%s", kProjectLicenseSummary.data());
    ImGui::TextWrapped("%s", kProjectRedistributionSummary.data());
    ImGui::TextWrapped("%s", kProjectWarrantySummary.data());
    ImGui::TextWrapped("%s", kProjectLicenseLocation.data());

    if (ImGui::CollapsingHeader("Third-party notices", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BulletText("Dear ImGui (docking branch): MIT License");
        ImGui::BulletText("readerwriterqueue: Simplified BSD License");
        ImGui::BulletText("doctest: MIT License");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Impulse also links against system-provided libraries including SDL3, FFmpeg, "
            "PipeWire, and systemd/libsystemd.");
        ImGui::TextWrapped(
            "These libraries are not redistributed in this repository and remain under their "
            "respective upstream and distribution licenses.");
        ImGui::TextWrapped(
            "A summary of bundled third-party license notices is included in THIRD_PARTY_NOTICES.md.");
    }

    ImGui::Spacing();
    if (ImGui::Button("Close"))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void MainWindow::renderNowPlayingTab(const std::shared_ptr<const TrackInfo>& info) {
    syncAlbumArtTexture(info);

    if (album_art_texture_) {
        float avail = ImGui::GetContentRegionAvail().x;
        float aspect = (album_art_tex_h_ > 0)
            ? static_cast<float>(album_art_tex_w_) / album_art_tex_h_ : 1.0f;
        if (avail > 0.0f) {
            float img_w = avail;
            float img_h = img_w / aspect;
            ImGui::Image(reinterpret_cast<ImTextureID>(album_art_texture_), ImVec2(img_w, img_h));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click to open album art panel");
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                show_album_art_window_ = true;
            ImGui::Spacing();
        }
    }

    if (!info) {
        ImGui::TextDisabled("No track loaded.");
        return;
    }

    ImGui::TextWrapped("%s", info->title.empty() ? info->source.displayName().c_str()
                                                 : info->title.c_str());
    auto wrapped_disabled_text = [](const std::string& text) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", text.c_str());
        ImGui::PopStyleColor();
    };
    if (!info->artist.empty())
        wrapped_disabled_text(info->artist);
    if (!info->album.empty())
        wrapped_disabled_text(info->album);

    const std::string summary = formatTrackSummary(*info);
    if (!summary.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", summary.c_str());
    }
}

void MainWindow::renderMetadataPanel() {
    auto info = frame_now_playing_ ? frame_now_playing_->track_info : nullptr;

    if (!info) {
        ImGui::TextDisabled("No metadata available.");
        return;
    }

    if (ImGui::BeginTable("metadata_table", 2,
                          ImGuiTableFlags_SizingStretchProp,
                          ImVec2(-1.0f, 0.0f))) {
        ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed, 88.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

        auto row = [](const char* key, const std::string& val) {
            if (val.empty())
                return;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", key);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", val.c_str());
        };

        row("Title", info->title);
        row("Artist", info->artist);
        row("Album", info->album);
        row("Year", info->year);
        row("Genre", info->genre);
        row("Track", info->track_number);
        row("Comment", info->comment);
        ImGui::EndTable();
    }

    if (!info->stream_metadata.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Stream Tags");
        renderTrackInfoFieldTable("metadata_stream_tags", info->stream_metadata, 132.0f);
    }

    if (!info->format_metadata.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Container Tags");
        renderTrackInfoFieldTable("metadata_format_tags", info->format_metadata, 132.0f);
    }
}

void MainWindow::renderTechInfoPanel() {
    auto info = frame_now_playing_ ? frame_now_playing_->track_info : nullptr;
    if (!info) {
        ImGui::TextDisabled("No technical info available.");
        return;
    }

    if (ImGui::BeginTable("tech_info_table", 2,
                          ImGuiTableFlags_SizingStretchProp,
                          ImVec2(-1.0f, 0.0f))) {
        ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed, 108.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

        auto row = [](const char* key, const std::string& val) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", key);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", val.c_str());
        };

        row("Codec", info->codec_name.empty() ? "--" : info->codec_name);
        row("Container", info->container_format.empty() ? "--" : info->container_format);
        row("Bitrate",
            info->bitrate_bps > 0
                ? std::format("{} kbps", info->bitrate_bps / 1000)
                : "--");
        row("Sample rate",
            info->sample_rate > 0
                ? std::format("{} Hz", info->sample_rate)
                : "--");
        row("Bit depth",
            info->bit_depth > 0
                ? std::format("{}-bit", info->bit_depth)
                : "-- (lossy)");
        row("Channels",
            info->channels > 0
                ? std::format("{} ({})", info->channels, info->channel_layout.empty() ? "--" : info->channel_layout)
                : "--");
        row("Duration", info->finite_duration && info->duration_seconds > 0.0
            ? formatTime(info->duration_seconds)
            : "Live");
        row("File size", formatBytes(info->file_size_bytes));
        row("Initial trim",
            std::format("{} sample(s)", info->initial_padding_samples));
        row("Trailing trim",
            std::format("{} sample(s)", info->trailing_padding_samples));
        row("Seek preroll",
            std::format("{} sample(s)", info->seek_preroll_samples));
        row("Trim mode", info->manual_skip_export_enabled
            ? "FFmpeg manual frame skip metadata"
            : "Codec padding fallback / none");
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("ReplayGain");
    if (ImGui::BeginTable("tech_rg_table", 2,
                          ImGuiTableFlags_SizingStretchProp,
                          ImVec2(-1.0f, 0.0f))) {
        ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed, 108.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

        auto rg_row = [&](const char* key, const std::optional<float>& value, const char* unit) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", key);
            ImGui::TableSetColumnIndex(1);
            if (value && unit[0] != '\0') {
                ImGui::Text("%.2f %s", *value, unit);
            } else if (value) {
                ImGui::Text("%.2f", *value);
            } else {
                ImGui::TextDisabled("N/A");
            }
        };

        rg_row("Track gain", info->rg_track_gain_db, "dB");
        rg_row("Track peak", info->rg_track_peak, "");
        rg_row("Album gain", info->rg_album_gain_db, "dB");
        rg_row("Album peak", info->rg_album_peak, "");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("Track PLR");
        ImGui::TableSetColumnIndex(1);
        if (auto track_plr = ReplayGain::plrLu(*info, settings_.plr_reference_lufs)) {
            ImGui::Text("%.2f LU", *track_plr);
        } else {
            ImGui::TextDisabled("N/A");
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextDisabled(info->source.isUrl() ? "Source URL" : "File");
    ImGui::TextWrapped("%s", info->source.string().c_str());

    if (!info->ffmpeg_analysis.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("FFmpeg Analysis");
        renderTrackInfoFieldTable("tech_ffmpeg_analysis", info->ffmpeg_analysis, 156.0f);
    }
}

void MainWindow::handleKeyboardShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput)
        return;

    if (ImGui::IsKeyPressed(ImGuiKey_F1, false))
        open_shortcuts_popup_ = true;

    if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
        togglePlayPause();

    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !selectedPlaylistIndices().empty())
        removeSelectedPlaylistItems();

    if ((ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) &&
        !selectedPlaylistIndices().empty()) {
        if (auto selected = selectedPlaylistIndices(); !selected.empty())
            openTrackAt(selected.front());
    }

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        clearPlaylistSelection();
        auto& view_state = playlist_view_states_[workspace_.activePlaylistId()];
        for (const auto& track : activePlaylist().tracks())
            view_state.selection.insert(track.id);
    }

    if (!io.KeyCtrl && !io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_R, false))
        cycleRepeatMode();

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false))
        requestNewPlaylist();

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false))
        requestOpenPlaylistDialog();

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_L, false))
        requestAddUrl();

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && !io.KeyShift)
        requestSavePlaylistDialog(workspace_.activePlaylistId());

    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false))
        shufflePlaylist(workspace_.activePlaylistId());

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_W, false) && workspace_.playlists().size() > 1)
        requestClosePlaylist(workspace_.activePlaylistId());

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Comma, false)) {
        syncSettingsFormFromCurrent();
        show_settings_window_ = true;
    }

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q, false))
        requestQuit();

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false))
        openPrevTrack();

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_RightArrow, false))
        openNextTrack();

    if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false))
        seekRelative(-5.0);

    if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_RightArrow, false))
        seekRelative(5.0);

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_UpArrow, false))
        adjustVolume(0.05f);

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_DownArrow, false))
        adjustVolume(-0.05f);
}

void MainWindow::pushPendingDialogResult(PendingDialogResult result) {
    pushPendingDialogResult(*dialog_callback_state_, std::move(result));
}

void MainWindow::pushPendingDialogResult(DialogCallbackState& state, PendingDialogResult result) {
    std::scoped_lock lock(state.mutex);
    if (!state.accepting_results)
        return;
    state.pending_results.push_back(std::move(result));
}

void SDLCALL MainWindow::onOpenPlaylistDialogComplete(void* userdata,
                                                      const char* const* filelist,
                                                      int /*filter*/) {
    std::unique_ptr<DialogRequestContext> request(static_cast<DialogRequestContext*>(userdata));
    if (!request || !request->state)
        return;

    PendingDialogResult result;
    result.action = request->action;
    result.playlist_id = request->playlist_id;
    if (filelist == nullptr) {
        result.error_message = SDL_GetError();
    } else {
        for (size_t i = 0; filelist[i] != nullptr; ++i)
            result.paths.emplace_back(filelist[i]);
    }

    pushPendingDialogResult(*request->state, std::move(result));
}

void SDLCALL MainWindow::onSavePlaylistDialogComplete(void* userdata,
                                                      const char* const* filelist,
                                                      int /*filter*/) {
    std::unique_ptr<DialogRequestContext> request(static_cast<DialogRequestContext*>(userdata));
    if (!request || !request->state)
        return;

    PendingDialogResult result;
    result.action = request->action;
    result.playlist_id = request->playlist_id;
    if (filelist == nullptr) {
        result.error_message = SDL_GetError();
    } else if (filelist[0] != nullptr) {
        result.paths.emplace_back(filelist[0]);
    }

    pushPendingDialogResult(*request->state, std::move(result));
}

void MainWindow::processPendingDialogResults() {
    std::vector<PendingDialogResult> results;
    {
        std::scoped_lock lock(dialog_callback_state_->mutex);
        results.swap(dialog_callback_state_->pending_results);
    }

    for (auto& result : results) {
        if (result.action == PendingDialogResult::Action::SavePlaylist)
            clearPendingSaveRequest();

        if (!result.error_message.empty()) {
            status_message_ = std::format("Playlist dialog failed: {}", result.error_message);
            continue;
        }

        if (result.action == PendingDialogResult::Action::OpenPlaylist) {
            for (const auto& path : result.paths)
                openPlaylistFile(path);
        } else if (result.action == PendingDialogResult::Action::SavePlaylist && !result.paths.empty()) {
            savePlaylistToFile(result.playlist_id, result.paths.front());
        }
    }
}

void MainWindow::importPendingExternalDrops() {
    if (pending_external_drop_paths_.empty())
        return;

    std::vector<std::filesystem::path> dropped_paths = pending_external_drop_paths_;
    pending_external_drop_paths_.clear();

    std::vector<std::filesystem::path> audio_paths;
    size_t playlist_count = 0;
    for (const auto& path : dropped_paths) {
        if (isPlaylistFile(path)) {
            openPlaylistFile(path);
            ++playlist_count;
            continue;
        }

        if (std::filesystem::is_directory(path)) {
            auto files = FileBrowser::collectAudioFiles(path, true);
            audio_paths.insert(audio_paths.end(), files.begin(), files.end());
            continue;
        }

        audio_paths.push_back(path);
    }

    if (!audio_paths.empty())
        addPathsToPlaylist(audio_paths, false, false);

    status_message_ = std::format("Imported {} dropped item(s).", dropped_paths.size());
    if (playlist_count > 0 && audio_paths.empty())
        redraw_requested_ = true;
}

void MainWindow::addTrackToPlaylist(const MediaSource& source,
                                    bool play_immediately,
                                    bool insert_next) {
    if (source.isFile()) {
        addTrackToPlaylist(source.path, play_immediately, insert_next);
        return;
    }

    PlaylistManager& playlist = activePlaylist();
    const size_t insert_index = insert_next && playlist.hasCurrentTrack()
        ? std::min(playlist.currentIndex() + 1, playlist.size())
        : playlist.size();

    if (insert_next)
        playlist.insertNext(source, nullptr);
    else
        playlist.addTrack(source, nullptr);

    notifyPlaylistStructureChanged();

    if (play_immediately)
        openTrackAt(insert_index);
}

void MainWindow::addTrackToPlaylist(const std::filesystem::path& path,
                                    bool play_immediately,
                                    bool insert_next) {
    std::error_code exists_ec;
    if (!std::filesystem::exists(path, exists_ec) || exists_ec || !FileBrowser::isAudioFile(path))
        return;

    PlaylistManager& playlist = activePlaylist();
    std::optional<TrackInfo> metadata;
    if (auto result = MetadataReader::read(
            path, MetadataReadOptions{.decode_album_art = false});
        result.has_value())
        metadata = std::move(result.value());

    const size_t insert_index = insert_next && playlist.hasCurrentTrack()
        ? std::min(playlist.currentIndex() + 1, playlist.size())
        : playlist.size();

    if (insert_next)
        playlist.insertNext(path, metadata ? &*metadata : nullptr);
    else
        playlist.addTrack(path, metadata ? &*metadata : nullptr);

    notifyPlaylistStructureChanged();

    if (play_immediately)
        openTrackAt(insert_index);
}

void MainWindow::addSourcesToPlaylist(const std::vector<MediaSource>& sources,
                                      bool play_first,
                                      bool insert_next) {
    if (sources.empty()) return;

    PlaylistManager& playlist = activePlaylist();
    const size_t size_before = playlist.size();
    const size_t first_insert_index = insert_next && playlist.hasCurrentTrack()
        ? std::min(playlist.currentIndex() + 1, playlist.size())
        : playlist.size();

    if (insert_next) {
        for (auto it = sources.rbegin(); it != sources.rend(); ++it)
            addTrackToPlaylist(*it, false, true);
    } else {
        for (const auto& source : sources)
            addTrackToPlaylist(source, false, false);
    }

    const size_t added_count = activePlaylist().size() - size_before;
    status_message_ = std::format("Added {} track(s) to {}.", added_count, activePlaylistDocument().name);

    if (play_first && activePlaylist().size() > first_insert_index)
        openTrackAt(first_insert_index);
}

void MainWindow::addPathsToPlaylist(const std::vector<std::filesystem::path>& paths,
                                    bool play_first,
                                    bool insert_next) {
    if (paths.empty()) return;

    PlaylistManager& playlist = activePlaylist();
    const size_t size_before = playlist.size();
    const size_t first_insert_index = insert_next && playlist.hasCurrentTrack()
        ? std::min(playlist.currentIndex() + 1, playlist.size())
        : playlist.size();

    if (insert_next) {
        for (auto it = paths.rbegin(); it != paths.rend(); ++it)
            addTrackToPlaylist(*it, false, true);
    } else {
        for (const auto& path : paths)
            addTrackToPlaylist(path, false, false);
    }

    const size_t added_count = activePlaylist().size() - size_before;
    status_message_ = std::format("Added {} track(s) to {}.", added_count, activePlaylistDocument().name);

    if (play_first && activePlaylist().size() > first_insert_index)
        openTrackAt(first_insert_index);
}

void MainWindow::replacePlaylistWithPaths(const std::vector<std::filesystem::path>& paths,
                                          bool play_first) {
    activePlaylist().clear();
    clearPlaylistSelection();
    notifyPlaylistStructureChanged();
    if (paths.empty())
        return;

    addPathsToPlaylist(paths, play_first, false);
}

void MainWindow::addDirectoryToPlaylist(const std::filesystem::path& path,
                                        bool recursive,
                                        bool play_first,
                                        bool insert_next) {
    addPathsToPlaylist(FileBrowser::collectAudioFiles(path, recursive), play_first, insert_next);
}

bool MainWindow::playFirstTrackIfStopped() {
    if (app_.playbackStatus() != PlaybackStatus::Stopped)
        return false;

    if (frame_now_playing_ &&
        frame_now_playing_->playlist_tab_id == 0 &&
        frame_now_playing_->track_info &&
        !frame_now_playing_->track_info->source.empty()) {
        clearPendingEndOfTrackAdvance();
        seek_drag_progress_.reset();
        app_.commandOpenFile(frame_now_playing_->track_info->source);
        return true;
    }

    if (auto* playback_playlist = playbackPlaylistDocument();
        playback_playlist && !playback_playlist->playlist.empty()) {
        const size_t index = playback_playlist->playlist.hasCurrentTrack()
            ? playback_playlist->playlist.currentIndex()
            : size_t{0};
        openTrackAt(playback_playlist->id, index);
        return true;
    }

    if (activePlaylist().empty())
        return false;

    const size_t index = activePlaylist().hasCurrentTrack()
        ? activePlaylist().currentIndex()
        : size_t{0};
    openTrackAt(workspace_.activePlaylistId(), index);
    return true;
}

void MainWindow::togglePlayPause() {
    if (app_.playbackStatus() == PlaybackStatus::Playing ||
        app_.playbackStatus() == PlaybackStatus::Buffering) {
        app_.commandPause();
        return;
    }

    if (!playFirstTrackIfStopped())
        app_.commandPlay();
}

void MainWindow::requestQuit() {
    SDL_Event quit_event{};
    quit_event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit_event);
}

void MainWindow::processMprisCommands() {
    for (const auto& command : mpris_.takePendingCommands())
        handleMprisCommand(command);
}

void MainWindow::handleMprisCommand(const MprisCommand& command) {
    switch (command.type) {
        case MprisCommandType::Raise:
            if (window_) {
                SDL_RestoreWindow(window_);
                SDL_RaiseWindow(window_);
            }
            break;

        case MprisCommandType::Quit:
            requestQuit();
            break;

        case MprisCommandType::Play:
            if (app_.playbackStatus() == PlaybackStatus::Paused)
                app_.commandPlay();
            else
                playFirstTrackIfStopped();
            break;

        case MprisCommandType::Pause:
            app_.commandPause();
            break;

        case MprisCommandType::PlayPause:
            togglePlayPause();
            break;

        case MprisCommandType::Stop:
            clearPendingEndOfTrackAdvance();
            seek_drag_progress_.reset();
            app_.commandStop();
            break;

        case MprisCommandType::Next:
            openNextTrack();
            break;

        case MprisCommandType::Previous:
            openPrevTrack();
            break;

        case MprisCommandType::SeekRelative: {
            if (!frame_now_playing_)
                break;

            const double duration = app_.durationSeconds();
            if (duration <= 0.0)
                break;

            seekTo(std::clamp(app_.positionSeconds() + mprisTimeToSeconds(command.position_us),
                              0.0,
                              duration));
            break;
        }

        case MprisCommandType::SetPosition: {
            if (!frame_now_playing_)
                break;

            if (buildMprisTrackId(*frame_now_playing_) != command.track_id)
                break;

            const double duration = app_.durationSeconds();
            if (duration <= 0.0)
                break;

            seekTo(std::clamp(mprisTimeToSeconds(command.position_us), 0.0, duration));
            break;
        }

        case MprisCommandType::SetVolume:
            app_.commandSetVolume(std::clamp(static_cast<float>(command.volume), 0.0f, 1.0f));
            break;

        case MprisCommandType::SetLoopStatus:
            applyRepeatMode(mprisLoopStatusToRepeatMode(command.loop_status));
            break;

        case MprisCommandType::OpenUri: {
            const auto source = mediaSourceFromOpenUri(command.uri);
            if (!source)
                break;

            clearPendingEndOfTrackAdvance();
            seek_drag_progress_.reset();
            if (isPlaylistSource(*source))
                openPlaylistSource(*source);
            else
                app_.commandOpenFile(*source);
            break;
        }
    }
}

void MainWindow::seekTo(double target_seconds) {
    auto now_playing = app_.currentNowPlaying();
    if (!now_playing)
        return;
    if (!now_playing->track_info || !now_playing->track_info->seekable)
        return;

    const double duration = app_.durationSeconds();
    if (duration <= 0.0)
        return;

    const double target = std::clamp(target_seconds, 0.0, duration);
    seek_drag_progress_.reset();
    app_.commandSeek(target);
    pending_mpris_seek_track_id_ = buildMprisTrackId(*now_playing);
    pending_mpris_seek_target_us_ = secondsToMprisTime(target);
}

void MainWindow::seekRelative(double delta_seconds) {
    if (!frame_now_playing_)
        return;
    if (!frame_now_playing_->track_info || !frame_now_playing_->track_info->seekable)
        return;

    const double duration = app_.durationSeconds();
    if (duration <= 0.0)
        return;

    seekTo(std::clamp(app_.positionSeconds() + delta_seconds, 0.0, duration));
}

void MainWindow::adjustVolume(float delta) {
    app_.commandSetVolume(std::clamp(app_.volume() + delta, 0.0f, 1.0f));
}

void MainWindow::requestAddUrl() {
    add_url_buffer_.fill('\0');
    open_add_url_popup_ = true;
}

void MainWindow::applyRepeatMode(RepeatMode mode) {
    if (settings_.repeat_mode == mode)
        return;

    clearPendingEndOfTrackAdvance();
    app_.invalidatePendingGaplessRequests();
    settings_.repeat_mode = mode;
    if (!SettingsStorage::save(settings_path_, settings_))
        status_message_ = "Repeat mode changed for this session, but settings could not be saved.";
    redraw_requested_ = true;
}

void MainWindow::cycleRepeatMode() {
    switch (settings_.repeat_mode) {
        case RepeatMode::Off:
            applyRepeatMode(RepeatMode::Playlist);
            break;
        case RepeatMode::Playlist:
            applyRepeatMode(RepeatMode::Track);
            break;
        case RepeatMode::Track:
        default:
            applyRepeatMode(RepeatMode::Off);
            break;
    }
}

void MainWindow::shufflePlaylist(uint64_t playlist_id) {
    auto* playlist = playlistDocument(playlist_id);
    if (!playlist || playlist->playlist.size() < 2)
        return;

    playlist->playlist.shuffle();
    clearPendingEndOfTrackAdvance();
    seek_drag_progress_.reset();
    notifyPlaylistStructureChanged(playlist_id);
    status_message_ = std::format("Shuffled {}.", playlist->name);
}

void MainWindow::openTrackAt(size_t index) {
    openTrackAt(workspace_.activePlaylistId(), index);
}

void MainWindow::openTrackAt(uint64_t playlist_id, size_t index) {
    auto* document = playlistDocument(playlist_id);
    if (!document || index >= document->playlist.size())
        return;

    clearPendingEndOfTrackAdvance();
    seek_drag_progress_.reset();
    document->playlist.setCurrentIndex(index);
    clearPlaylistSelection(playlist_id);
    auto& view_state = playlist_view_states_[playlist_id];
    view_state.selection.insert(document->playlist.tracks()[index].id);
    view_state.last_clicked_index = index;
    app_.commandOpenFile(document->playlist.tracks()[index].source,
                         playlist_id,
                         document->playlist.tracks()[index].id);
}

void MainWindow::removeSelectedPlaylistItems() {
    const auto indices = selectedPlaylistIndices();
    if (indices.empty())
        return;

    activePlaylist().removeTracks(indices);
    clearPlaylistSelection();
    notifyPlaylistStructureChanged();
    status_message_ = std::format("Removed {} track(s) from {}.", indices.size(), activePlaylistDocument().name);
}

void MainWindow::clearPlaylistSelection() {
    clearPlaylistSelection(workspace_.activePlaylistId());
}

void MainWindow::clearPlaylistSelection(uint64_t playlist_id) {
    auto& view_state = playlist_view_states_[playlist_id];
    view_state.selection.clear();
    view_state.last_clicked_index.reset();
}

void MainWindow::selectPlaylistIndex(size_t index, bool toggle, bool range_select) {
    PlaylistManager& playlist = activePlaylist();
    auto& view_state = playlist_view_states_[workspace_.activePlaylistId()];
    if (index >= playlist.size())
        return;

    const uint64_t id = playlist.tracks()[index].id;
    if (range_select && view_state.last_clicked_index) {
        const size_t first = std::min(*view_state.last_clicked_index, index);
        const size_t last  = std::max(*view_state.last_clicked_index, index);
        if (!toggle)
            view_state.selection.clear();
        for (size_t i = first; i <= last; ++i)
            view_state.selection.insert(playlist.tracks()[i].id);
    } else if (toggle) {
        if (!view_state.selection.erase(id))
            view_state.selection.insert(id);
        view_state.last_clicked_index = index;
        return;
    } else {
        view_state.selection.clear();
        view_state.selection.insert(id);
    }

    view_state.last_clicked_index = index;
}

bool MainWindow::isPlaylistSelected(uint64_t id) const {
    auto it = playlist_view_states_.find(workspace_.activePlaylistId());
    if (it == playlist_view_states_.end())
        return false;
    return it->second.selection.contains(id);
}

std::vector<size_t> MainWindow::selectedPlaylistIndices() const {
    std::vector<size_t> indices;
    const auto it = playlist_view_states_.find(workspace_.activePlaylistId());
    if (it == playlist_view_states_.end())
        return indices;

    const auto& selection = it->second.selection;
    const PlaylistManager& playlist = activePlaylist();
    for (size_t i = 0; i < playlist.size(); ++i) {
        if (selection.contains(playlist.tracks()[i].id))
            indices.push_back(i);
    }
    return indices;
}

void MainWindow::syncPlaylistCursorToNowPlaying(uint64_t playlist_id, uint64_t playlist_item_id) {
    if (playlist_id == 0 || playlist_item_id == 0)
        return;

    auto* document = playlistDocument(playlist_id);
    if (!document)
        return;

    const auto current_index = document->playlist.indexOf(playlist_item_id);
    if (!current_index)
        return;

    document->playlist.setCurrentIndex(*current_index);
}

void MainWindow::notifyPlaylistStructureChanged() {
    notifyPlaylistStructureChanged(workspace_.activePlaylistId());
}

void MainWindow::notifyPlaylistStructureChanged(uint64_t playlist_id) {
    clearPendingEndOfTrackAdvance();
    if (auto* document = playlistDocument(playlist_id)) {
        ++document->revision;
        syncPlaylistRevision(playlist_id);
    }
}

void MainWindow::clearPendingEndOfTrackAdvance() {
    pending_end_of_track_advance_ = false;
    pending_end_of_track_track_.reset();
}

void MainWindow::syncPendingEndOfTrackAdvanceWithNowPlaying() {
    if (!pending_end_of_track_advance_)
        return;

    if (playbackTrackInstanceMatches(pending_end_of_track_track_, frame_now_playing_))
        return;

    clearPendingEndOfTrackAdvance();
}

void MainWindow::syncPlaylistRevision(uint64_t playlist_id) {
    if (const auto* document = playlistDocument(playlist_id))
        app_.notifyPlaylistChanged(playlist_id, document->revision);
}

void MainWindow::syncAllPlaylistRevisions() {
    for (const auto& playlist : workspace_.playlists())
        app_.notifyPlaylistChanged(playlist.id, playlist.revision);
}

void MainWindow::syncAlbumArtTexture(const std::shared_ptr<const TrackInfo>& info) {
    if (sameSharedOwner(info, last_track_info_))
        return;
    last_track_info_ = info;

    if (album_art_texture_) {
        SDL_DestroyTexture(album_art_texture_);
        album_art_texture_ = nullptr;
    }

    if (!info || info->album_art_rgba.empty()) return;

    album_art_texture_ = SDL_CreateTexture(renderer_,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STATIC,
        info->album_art_width,
        info->album_art_height);

    if (!album_art_texture_) return;

    SDL_UpdateTexture(album_art_texture_, nullptr,
                      info->album_art_rgba.data(),
                      info->album_art_width * 4);
    album_art_tex_w_ = info->album_art_width;
    album_art_tex_h_ = info->album_art_height;
}

void MainWindow::syncLyricsDocument(const std::shared_ptr<const TrackInfo>& info) {
    if (sameSharedOwner(info, lyrics_track_info_))
        return;

    lyrics_track_info_ = info;
    lyrics_document_ = info ? parseLyrics(info->lyrics) : LyricsDocument{};
    last_active_timed_lyric_line_.reset();
    lyrics_should_autoscroll_ = true;
}

void MainWindow::beginRenamePlaylist(uint64_t playlist_id) {
    const auto* document = playlistDocument(playlist_id);
    if (!document)
        return;

    rename_playlist_id_ = playlist_id;
    std::snprintf(rename_playlist_buffer_.data(),
                  rename_playlist_buffer_.size(),
                  "%s",
                  document->name.c_str());
    open_rename_playlist_popup_ = true;
}

void MainWindow::renderRenamePlaylistPopup() {
    if (open_rename_playlist_popup_) {
        ImGui::OpenPopup("Rename Playlist");
        open_rename_playlist_popup_ = false;
    }

    if (!ImGui::BeginPopupModal("Rename Playlist", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputText("Name", rename_playlist_buffer_.data(), rename_playlist_buffer_.size());

    if (ImGui::Button("Save")) {
        if (workspace_.renamePlaylist(rename_playlist_id_, rename_playlist_buffer_.data())) {
            status_message_ = std::format("Renamed playlist to {}.", rename_playlist_buffer_.data());
            ImGui::CloseCurrentPopup();
        } else {
            status_message_ = "Playlist name cannot be empty.";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void MainWindow::renderAddUrlPopup() {
    if (open_add_url_popup_) {
        ImGui::OpenPopup("Add URL");
        open_add_url_popup_ = false;
    }

    if (!ImGui::BeginPopupModal("Add URL", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::SetNextItemWidth(420.0f);
    ImGui::InputTextWithHint("URL",
                             "http://example.com/stream.ogg",
                             add_url_buffer_.data(),
                             add_url_buffer_.size());

    if (ImGui::Button("Add")) {
        const MediaSource source = MediaSource::fromSerialized(add_url_buffer_.data());
        if (!source.isUrl() || source.empty()) {
            status_message_ = "Enter a valid URL.";
        } else if (isPlaylistSource(source)) {
            const auto loaded = M3U8Playlist::load(source);
            if (!loaded) {
                status_message_ = std::format("Failed to load playlist URL {}.", source.string());
            } else {
                std::vector<MediaSource> playable_sources;
                size_t unsupported_count = 0;
                for (const auto& entry : loaded->entries) {
                    if (entry.isUrl() || FileBrowser::isAudioFile(entry.path))
                        playable_sources.push_back(entry);
                    else
                        ++unsupported_count;
                }

                if (!playable_sources.empty())
                    addSourcesToPlaylist(playable_sources, false, false);
                status_message_ = std::format("Loaded {} ({} track(s), {} missing, {} unsupported).",
                                              source.displayName(),
                                              playable_sources.size(),
                                              loaded->missing_entries,
                                              unsupported_count);
                ImGui::CloseCurrentPopup();
            }
        } else {
            addTrackToPlaylist(source, false, false);
            status_message_ = std::format("Added {}.", source.string());
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void MainWindow::requestClosePlaylist(uint64_t playlist_id) {
    closePlaylist(playlist_id);
}

void MainWindow::requestNewPlaylist(std::string name) {
    auto& playlist = workspace_.createPlaylist(std::move(name));
    playlist.playlist.setPlrReferenceLufs(settings_.plr_reference_lufs);
    playlist_view_states_.try_emplace(playlist.id);
    pending_focus_playlist_id_ = playlist.id;
    syncPlaylistRevision(playlist.id);
    status_message_ = std::format("Created playlist {}.", playlist.name);
}

void MainWindow::requestOpenPlaylistDialog() {
    auto request = std::make_unique<DialogRequestContext>();
    request->state = dialog_callback_state_;
    request->action = PendingDialogResult::Action::OpenPlaylist;
    request->default_location = browser_.currentPath().string();
    auto* raw_request = request.release();
    SDL_ShowOpenFileDialog(&MainWindow::onOpenPlaylistDialogComplete,
                           raw_request,
                           window_,
                           kPlaylistDialogFilters,
                           SDL_arraysize(kPlaylistDialogFilters),
                           raw_request->default_location.c_str(),
                           true);
}

void MainWindow::requestSavePlaylistDialog(uint64_t playlist_id) {
    const auto* playlist = playlistDocument(playlist_id);
    if (!playlist)
        return;

    if (!beginPendingSaveRequest()) {
        status_message_ = "A playlist save dialog is already open.";
        return;
    }

    auto request = std::make_unique<DialogRequestContext>();
    request->state = dialog_callback_state_;
    request->action = PendingDialogResult::Action::SavePlaylist;
    request->playlist_id = playlist_id;
    request->default_location =
        (browser_.currentPath() / playlistFileDialogName(playlist->name)).string();
    auto* raw_request = request.release();
    SDL_ShowSaveFileDialog(&MainWindow::onSavePlaylistDialogComplete,
                           raw_request,
                           window_,
                           kPlaylistDialogFilters,
                           SDL_arraysize(kPlaylistDialogFilters),
                           raw_request->default_location.c_str());
}

bool MainWindow::beginPendingSaveRequest() {
    if (save_dialog_open_)
        return false;

    save_dialog_open_ = true;
    return true;
}

void MainWindow::clearPendingSaveRequest() {
    save_dialog_open_ = false;
}

void MainWindow::openPlaylistSource(const MediaSource& source) {
    const auto loaded = M3U8Playlist::load(source);
    if (!loaded) {
        status_message_ = std::format("Failed to open playlist {}.", source.displayName());
        return;
    }

    std::vector<MediaSource> playable_sources;
    size_t unsupported_count = 0;
    for (const auto& entry : loaded->entries) {
        if (entry.isUrl() || FileBrowser::isAudioFile(entry.path))
            playable_sources.push_back(entry);
        else
            ++unsupported_count;
    }

    requestNewPlaylist(source.stem());
    if (!playable_sources.empty())
        addSourcesToPlaylist(playable_sources, false, false);

    status_message_ = std::format("Opened {} ({} track(s), {} missing, {} unsupported).",
                                  source.displayName(),
                                  playable_sources.size(),
                                  loaded->missing_entries,
                                  unsupported_count);
}

void MainWindow::openPlaylistFile(const std::filesystem::path& path) {
    openPlaylistSource(MediaSource::fromPath(path));
}

void MainWindow::savePlaylistToFile(uint64_t playlist_id, const std::filesystem::path& path) {
    const auto* playlist = playlistDocument(playlist_id);
    if (!playlist)
        return;

    std::filesystem::path target_path = path;
    if (!target_path.has_extension())
        target_path += ".m3u8";

    std::vector<MediaSource> track_sources;
    track_sources.reserve(playlist->playlist.size());
    for (const auto& track : playlist->playlist.tracks())
        track_sources.push_back(track.source);

    if (!M3U8Playlist::save(target_path, track_sources)) {
        status_message_ = std::format("Failed to save playlist {}.", target_path.filename().string());
        return;
    }

    status_message_ = std::format("Saved {}.", target_path.filename().string());
}

void MainWindow::closePlaylist(uint64_t playlist_id) {
    if (workspace_.playlists().size() <= 1)
        return;

    app_.forgetPlaylist(playlist_id);
    if (playbackPlaylistId() == playlist_id) {
        app_.commandStop();
        clearPendingEndOfTrackAdvance();
        seek_drag_progress_.reset();
    }

    playlist_view_states_.erase(playlist_id);
    workspace_.closePlaylist(playlist_id);
    workspace_.ensureDefaultPlaylist();
    playlist_view_states_.try_emplace(workspace_.activePlaylistId());
    syncAllPlaylistRevisions();
    status_message_ = "Playlist closed.";
}

bool MainWindow::isPlaylistFile(const std::filesystem::path& path) const {
    std::string ext = path.extension().string();
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".m3u8" || ext == ".m3u";
}

PlaylistDocument& MainWindow::activePlaylistDocument() {
    return workspace_.activePlaylist();
}

const PlaylistDocument& MainWindow::activePlaylistDocument() const {
    return workspace_.activePlaylist();
}

PlaylistManager& MainWindow::activePlaylist() {
    return workspace_.activePlaylist().playlist;
}

const PlaylistManager& MainWindow::activePlaylist() const {
    return workspace_.activePlaylist().playlist;
}

uint64_t MainWindow::playbackPlaylistId() const {
    return frame_now_playing_ ? frame_now_playing_->playlist_tab_id : 0;
}

PlaylistDocument* MainWindow::playbackPlaylistDocument() {
    return playlistDocument(playbackPlaylistId());
}

const PlaylistDocument* MainWindow::playbackPlaylistDocument() const {
    return playlistDocument(playbackPlaylistId());
}

PlaylistDocument* MainWindow::playlistDocument(uint64_t playlist_id) {
    return workspace_.playlistById(playlist_id);
}

const PlaylistDocument* MainWindow::playlistDocument(uint64_t playlist_id) const {
    return workspace_.playlistById(playlist_id);
}

std::optional<size_t> MainWindow::playbackAdvanceIndex(const PlaylistDocument& playlist) const {
    const uint64_t audible_playlist_item_id =
        frame_now_playing_ && frame_now_playing_->playlist_tab_id == playlist.id
        ? frame_now_playing_->playlist_item_id
        : 0;

    return ::playbackAdvanceIndex(playlist.playlist,
                                  audible_playlist_item_id,
                                  settings_.repeat_mode);
}

std::optional<size_t> MainWindow::manualAdvanceIndex(const PlaylistDocument& playlist, bool forward) const {
    if (!playlist.playlist.hasCurrentTrack())
        return std::nullopt;

    const size_t current_index = playlist.playlist.currentIndex();
    if (forward) {
        const size_t next_index = current_index + 1;
        if (next_index < playlist.playlist.size())
            return next_index;
        if (settings_.repeat_mode == RepeatMode::Playlist && !playlist.playlist.empty())
            return size_t{0};
        return std::nullopt;
    }

    if (current_index > 0)
        return current_index - 1;
    if (settings_.repeat_mode == RepeatMode::Playlist && !playlist.playlist.empty())
        return playlist.playlist.size() - 1;
    return std::nullopt;
}

void MainWindow::scheduleGaplessAdvanceTrack() {
    auto* playlist = playbackPlaylistDocument();
    if (!playlist || !playlist->playlist.hasCurrentTrack())
        return;

    const uint64_t audible_playlist_item_id =
        frame_now_playing_ && frame_now_playing_->playlist_tab_id == playlist->id
        ? frame_now_playing_->playlist_item_id
        : 0;
    const PlaybackAdvanceDecision advance =
        playbackAdvanceDecision(playlist->playlist,
                                audible_playlist_item_id,
                                settings_.repeat_mode);
    if (!advance.handled_current_track)
        return;

    pending_end_of_track_advance_ = true;
    pending_end_of_track_track_ = playbackTrackInstance(frame_now_playing_);
    if (!advance.next_index)
        return;

    const PlaylistItem& next = playlist->playlist.tracks()[*advance.next_index];
    const bool can_gapless = frame_now_playing_ &&
        frame_now_playing_->track_info &&
        !frame_now_playing_->track_info->is_stream &&
        next.source.isFile();
    if (can_gapless)
        app_.commandOpenFileGapless(next.source, playlist->id, next.id, playlist->revision);
}

void MainWindow::openNextTrack() {
    auto* playlist = playbackPlaylistDocument();
    if (!playlist)
        playlist = playlistDocument(workspace_.activePlaylistId());
    if (!playlist)
        return;

    const auto next_index = manualAdvanceIndex(*playlist, true);
    if (!next_index)
        return;

    clearPendingEndOfTrackAdvance();
    seek_drag_progress_.reset();
    clearPlaylistSelection(playlist->id);
    auto& view_state = playlist_view_states_[playlist->id];
    playlist->playlist.setCurrentIndex(*next_index);
    view_state.selection.insert(playlist->playlist.tracks()[*next_index].id);
    view_state.last_clicked_index = *next_index;
    app_.commandOpenFile(playlist->playlist.tracks()[*next_index].source,
                         playlist->id,
                         playlist->playlist.tracks()[*next_index].id);
}

void MainWindow::openPrevTrack() {
    auto* playlist = playbackPlaylistDocument();
    if (!playlist)
        playlist = playlistDocument(workspace_.activePlaylistId());
    if (!playlist)
        return;

    const auto prev_index = manualAdvanceIndex(*playlist, false);
    if (!prev_index)
        return;

    clearPendingEndOfTrackAdvance();
    seek_drag_progress_.reset();
    clearPlaylistSelection(playlist->id);
    auto& view_state = playlist_view_states_[playlist->id];
    playlist->playlist.setCurrentIndex(*prev_index);
    view_state.selection.insert(playlist->playlist.tracks()[*prev_index].id);
    view_state.last_clicked_index = *prev_index;
    app_.commandOpenFile(playlist->playlist.tracks()[*prev_index].source,
                         playlist->id,
                         playlist->playlist.tracks()[*prev_index].id);
}

MprisSnapshot MainWindow::captureMprisSnapshot() const {
    MprisSnapshot snapshot;
    const PlaybackStatus playback_status = app_.playbackStatus();
    snapshot.playback_status = playback_status;
    snapshot.loop_status = repeatModeToMprisLoopStatus(settings_.repeat_mode);
    snapshot.shuffle = false;
    snapshot.rate = 1.0;
    snapshot.minimum_rate = 1.0;
    snapshot.maximum_rate = 1.0;
    snapshot.volume = std::max(0.0, static_cast<double>(app_.volume()));
    snapshot.can_control = true;

    const PlaylistDocument* navigation_playlist = playbackPlaylistDocument();
    if (!navigation_playlist)
        navigation_playlist = playlistDocument(workspace_.activePlaylistId());

    snapshot.can_go_next =
        navigation_playlist && manualAdvanceIndex(*navigation_playlist, true).has_value();
    snapshot.can_go_previous =
        navigation_playlist && manualAdvanceIndex(*navigation_playlist, false).has_value();
    snapshot.can_play = (navigation_playlist && !navigation_playlist->playlist.empty()) ||
                        frame_now_playing_ != nullptr;
    snapshot.can_pause = frame_now_playing_ != nullptr &&
                         playback_status != PlaybackStatus::Stopped &&
                         playback_status != PlaybackStatus::Error;
    snapshot.can_seek = frame_now_playing_ != nullptr &&
                        frame_now_playing_->track_info &&
                        frame_now_playing_->track_info->seekable &&
                        app_.durationSeconds() > 0.0 &&
                        playback_status != PlaybackStatus::Stopped &&
                        playback_status != PlaybackStatus::Error;
    snapshot.position_us = frame_now_playing_ ? secondsToMprisTime(app_.positionSeconds()) : 0;

    if (frame_now_playing_)
        snapshot.track = buildMprisTrackMetadata(*frame_now_playing_, app_.outputSampleRate());

    return snapshot;
}
