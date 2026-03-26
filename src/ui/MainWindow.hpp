#pragma once

#include "app/AppSettings.hpp"
#include "app/Application.hpp"
#include "browser/FileBrowser.hpp"
#include "core/Lyrics.hpp"
#include "core/MediaSource.hpp"
#include "core/PlaybackStatusUtils.hpp"
#include "mpris/MprisService.hpp"
#include "playlist/PlaylistWorkspace.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct SDL_Texture;

class MainWindow {
public:
    MainWindow(Application& app, FileBrowser& browser);
    ~MainWindow();

    // Returns false when the user requests quit.
    bool runFrame();

private:
    struct PlaylistViewState {
        std::unordered_set<uint64_t> selection;
        std::optional<size_t>        last_clicked_index;
    };

    struct PendingDialogResult {
        enum class Action {
            OpenPlaylist,
            SavePlaylist
        };

        Action                             action = Action::OpenPlaylist;
        uint64_t                           playlist_id = 0;
        std::vector<std::filesystem::path> paths;
        std::string                        error_message;
    };

    struct DialogCallbackState {
        std::mutex                     mutex;
        std::vector<PendingDialogResult> pending_results;
        bool                           accepting_results = true;
    };

    struct DialogRequestContext {
        std::shared_ptr<DialogCallbackState> state;
        PendingDialogResult::Action          action = PendingDialogResult::Action::OpenPlaylist;
        uint64_t                             playlist_id = 0;
        std::string                          default_location;
    };

    void initSDL();
    void initStoragePaths();
    void loadSettings();
    void loadSession();
    void saveSession() const;
    void syncSettingsFormFromCurrent();
    bool applySettingsForm();
    void initImGui();
    void shutdownImGui();
    void shutdownSDL();
    int  nextWaitTimeoutMs(bool minimized);
    std::chrono::milliseconds targetRenderInterval(bool minimized) const;
    bool processEvent(const SDL_Event& ev);
    bool pumpEvents(int wait_timeout_ms);
    bool isWindowMinimized() const;
    bool shouldRenderFrame(bool minimized) const;

    void renderLayout();
    void renderDockspaceHost();
    void buildDefaultDockLayout(ImGuiID dockspace_id);
    void renderMainMenu();
    void renderBrowserWindow();
    void renderPlaylistWindow();
    void renderTransportWindow();
    void renderInspectorWindow();
    void renderLyricsWindow();
    void renderAlbumArtWindow();
    void renderSettingsWindow();
    void renderRenamePlaylistPopup();
    void renderAddUrlPopup();
    void renderKeyboardShortcutsPopup();
    void renderAboutPopup();
    void renderNowPlayingTab(const std::shared_ptr<const TrackInfo>& info);
    void renderMetadataPanel();
    void renderTechInfoPanel();
    void handleKeyboardShortcuts();
    void importPendingExternalDrops();
    void processPendingDialogResults();
    void pushPendingDialogResult(PendingDialogResult result);
    static void pushPendingDialogResult(DialogCallbackState& state, PendingDialogResult result);
    static void SDLCALL onOpenPlaylistDialogComplete(void* userdata,
                                                     const char* const* filelist,
                                                     int filter);
    static void SDLCALL onSavePlaylistDialogComplete(void* userdata,
                                                     const char* const* filelist,
                                                     int filter);

    void addTrackToPlaylist(const MediaSource& source,
                            bool play_immediately,
                            bool insert_next);
    void addTrackToPlaylist(const std::filesystem::path& path,
                            bool play_immediately,
                            bool insert_next);
    void addSourcesToPlaylist(const std::vector<MediaSource>& sources,
                              bool play_first,
                              bool insert_next);
    void addPathsToPlaylist(const std::vector<std::filesystem::path>& paths,
                            bool play_first,
                            bool insert_next);
    void replacePlaylistWithPaths(const std::vector<std::filesystem::path>& paths,
                                  bool play_first);
    void addDirectoryToPlaylist(const std::filesystem::path& path,
                                bool recursive,
                                bool play_first,
                                bool insert_next);

    bool playFirstTrackIfStopped();
    void togglePlayPause();
    void requestQuit();
    void processMprisCommands();
    void handleMprisCommand(const MprisCommand& command);
    void seekTo(double target_seconds);
    void seekRelative(double delta_seconds);
    void adjustVolume(float delta);
    void applyRepeatMode(RepeatMode mode);
    void cycleRepeatMode();
    void shufflePlaylist(uint64_t playlist_id);
    void openTrackAt(size_t index);
    void openTrackAt(uint64_t playlist_id, size_t index);
    void removeSelectedPlaylistItems();
    void clearPlaylistSelection();
    void clearPlaylistSelection(uint64_t playlist_id);
    void selectPlaylistIndex(size_t index, bool toggle, bool range_select);
    [[nodiscard]] bool isPlaylistSelected(uint64_t id) const;
    [[nodiscard]] std::vector<size_t> selectedPlaylistIndices() const;
    [[nodiscard]] std::vector<const DirEntry*> visibleBrowserEntries() const;
    void applyPlaylistSortFromTable(ImGuiTableSortSpecs* sort_specs);
    void syncPlaylistCursorToNowPlaying(uint64_t playlist_id, uint64_t playlist_item_id);
    void notifyPlaylistStructureChanged();
    void notifyPlaylistStructureChanged(uint64_t playlist_id);
    void syncPlaylistRevision(uint64_t playlist_id);
    void syncAllPlaylistRevisions();
    void beginRenamePlaylist(uint64_t playlist_id);
    void requestClosePlaylist(uint64_t playlist_id);
    void requestNewPlaylist(std::string name = {});
    void requestAddUrl();
    void requestOpenPlaylistDialog();
    void requestSavePlaylistDialog(uint64_t playlist_id);
    [[nodiscard]] bool beginPendingSaveRequest();
    void clearPendingSaveRequest();
    void openPlaylistSource(const MediaSource& source);
    void openPlaylistFile(const std::filesystem::path& path);
    void savePlaylistToFile(uint64_t playlist_id, const std::filesystem::path& path);
    void closePlaylist(uint64_t playlist_id);
    [[nodiscard]] bool isPlaylistFile(const std::filesystem::path& path) const;
    [[nodiscard]] PlaylistDocument& activePlaylistDocument();
    [[nodiscard]] const PlaylistDocument& activePlaylistDocument() const;
    [[nodiscard]] PlaylistManager& activePlaylist();
    [[nodiscard]] const PlaylistManager& activePlaylist() const;
    [[nodiscard]] uint64_t playbackPlaylistId() const;
    [[nodiscard]] PlaylistDocument* playbackPlaylistDocument();
    [[nodiscard]] const PlaylistDocument* playbackPlaylistDocument() const;
    [[nodiscard]] PlaylistDocument* playlistDocument(uint64_t playlist_id);
    [[nodiscard]] const PlaylistDocument* playlistDocument(uint64_t playlist_id) const;
    [[nodiscard]] std::optional<size_t> playbackAdvanceIndex(const PlaylistDocument& playlist) const;
    [[nodiscard]] std::optional<size_t> manualAdvanceIndex(const PlaylistDocument& playlist,
                                                           bool forward) const;

    void clearPendingEndOfTrackAdvance();
    void syncPendingEndOfTrackAdvanceWithNowPlaying();
    void syncAlbumArtTexture(const std::shared_ptr<const TrackInfo>& info);
    void syncLyricsDocument(const std::shared_ptr<const TrackInfo>& info);
    void scheduleGaplessAdvanceTrack();
    void openNextTrack();
    void openPrevTrack();
    [[nodiscard]] MprisSnapshot captureMprisSnapshot() const;

    struct RenderSnapshot {
        PlaybackStatus playback_status = PlaybackStatus::Stopped;
        uint64_t       now_playing_item_id = 0;
        std::shared_ptr<const TrackInfo> track_info;
        int64_t        position_ticks = 0;
        int64_t        duration_ticks = 0;
        int64_t        bitrate_kbps = 0;
        float          peak_abs = 0.0f;
        bool           clipped_detected = false;
        size_t         ring_read_bucket = 0;
        size_t         ring_write_bucket = 0;
        int            volume_centidb = 0;

        bool operator==(const RenderSnapshot& other) const;
    };

    struct FrameIdling {
        std::chrono::milliseconds interactive_render_interval{33};
        std::chrono::milliseconds playback_idle_render_interval{33};
        std::chrono::milliseconds idle_render_interval{250};
        std::chrono::milliseconds minimized_render_interval{500};
        std::chrono::milliseconds interaction_boost_window{250};
        std::chrono::steady_clock::time_point last_event_at{};
        bool is_idling = false;
    };

    RenderSnapshot captureRenderSnapshot() const;

    Application&     app_;
    FileBrowser&     browser_;
    PlaylistWorkspace workspace_;
    MprisService      mpris_;

    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;

    // Album art texture, created/destroyed on main thread
    SDL_Texture*      album_art_texture_ = nullptr;
    int               album_art_tex_w_   = 0;
    int               album_art_tex_h_   = 0;
    std::shared_ptr<const TrackInfo> last_track_info_;
    std::shared_ptr<const TrackInfo> lyrics_track_info_;
    LyricsDocument                  lyrics_document_;
    std::optional<size_t>           last_active_timed_lyric_line_;
    bool                            lyrics_should_autoscroll_ = false;

    std::filesystem::path browser_selected_path_;
    std::vector<std::filesystem::path> pending_external_drop_paths_;
    std::vector<std::filesystem::path> drag_browser_paths_;
    std::vector<uint64_t>              drag_playlist_ids_;
    std::unordered_map<uint64_t, PlaylistViewState> playlist_view_states_;
    std::shared_ptr<DialogCallbackState>          dialog_callback_state_;

    std::string           imgui_ini_path_;
    std::filesystem::path session_path_;
    std::filesystem::path settings_path_;
    AppSettings           settings_;
    std::shared_ptr<NowPlayingTrack> frame_now_playing_;
    RenderSnapshot        frame_snapshot_;
    RenderSnapshot        last_rendered_snapshot_;
    FrameIdling           frame_idling_{};
    std::chrono::steady_clock::time_point last_render_at_{};
    bool                  has_rendered_frame_ = false;
    bool                  redraw_requested_ = true;
    bool                  should_build_default_layout_ = false;
    bool                  dock_layout_built_ = false;
    bool                  request_layout_reset_ = false;

    bool show_browser_window_   = true;
    bool show_playlist_window_  = true;
    bool show_transport_window_ = true;
    bool show_inspector_window_ = true;
    bool show_lyrics_window_    = true;
    bool show_album_art_window_ = false;
    bool show_settings_window_  = false;

    std::array<char, 1024> settings_browser_root_buffer_{};
    std::array<char, 1024> add_url_buffer_{};
    std::array<char, 256>  rename_playlist_buffer_{};
    float settings_buffer_ahead_seconds_ = 16.0f;
    float settings_replaygain_with_rg_db_ = 0.0f;
    float settings_replaygain_without_rg_db_ = 0.0f;
    float settings_plr_reference_lufs_ = -18.0f;
    std::optional<float> seek_drag_progress_;
    std::string status_message_;
    float bitrate_bar_peak_kbps_ = 0.0f;
    std::shared_ptr<const TrackInfo> bitrate_peak_track_;
    bool pending_end_of_track_advance_ = false;
    std::optional<PlaybackTrackInstance> pending_end_of_track_track_;
    std::optional<uint64_t> pending_end_of_track_target_item_id_;
    bool pending_error_advance_ = false;
    std::optional<int64_t> pending_mpris_seek_target_us_;
    std::string            pending_mpris_seek_track_id_;
    bool open_rename_playlist_popup_ = false;
    bool open_add_url_popup_ = false;
    bool open_shortcuts_popup_ = false;
    bool open_about_popup_ = false;
    bool save_dialog_open_ = false;
    uint64_t rename_playlist_id_ = 0;
    uint64_t pending_focus_playlist_id_ = 0;
};
