import path from "node:path";
import { promises as fs } from "node:fs";
import type {
  AppEvent,
  AppSettings,
  FilePropertiesSnapshot,
  InitialState,
  LyricsSnapshot,
  PropertyEntry,
  PlaybackCommand,
  PlaybackSnapshot,
  PlaylistCommand,
  SessionState,
  StatusSnapshot
} from "../../shared/types.js";
import {
  clamp,
  formatBitDepth,
  formatChannels,
  formatSampleRate
} from "../../shared/format.js";
import { ConfigStore } from "./config-store.js";
import { CoverArtService } from "./cover-art-service.js";
import { FileBrowserService } from "./file-browser.js";
import { LyricsService } from "./lyrics-service.js";
import { MetadataService } from "./metadata-service.js";
import { MetadataLoadQueue, type MetadataLoadPriority } from "./metadata-load-queue.js";
import { isAudioFile, isDirectory, listAudioFilesRecursive, normalizePath } from "./path-utils.js";
import { PlaylistManager } from "./playlist-manager.js";
import type { PlaybackAudioParams, PlaybackBackendState } from "./playback/backend.js";
import { MpvIpcBackend } from "./playback/mpv-ipc-backend.js";
import { checkRuntimeDependencies } from "./runtime-dependencies.js";
import { sanitizeAppSettings } from "./settings-utils.js";
import { SessionStore } from "./session-store.js";

interface EventTarget {
  emit(event: AppEvent): void;
}

function isValidRepeatMode(value: unknown): value is "off" | "all" | "one" {
  return value === "off" || value === "all" || value === "one";
}

function stringifyTagValue(value: string | number | Array<string | number>): string {
  if (Array.isArray(value)) {
    return value.map((entry) => String(entry)).join(", ");
  }
  return String(value);
}

function normalizeDisplayValue(value: string | null): string {
  if (!value) {
    return "unknown";
  }

  const trimmed = value.trim();
  return trimmed.length > 0 ? trimmed : "unknown";
}

function normalizeUpperDisplayValue(value: string | null): string {
  const normalized = normalizeDisplayValue(value);
  return normalized === "unknown" ? normalized : normalized.toUpperCase();
}

function inferContainerFromPath(filePath: string): string | null {
  const extension = path.extname(filePath).replace(/^\./, "").trim();
  if (!extension) {
    return null;
  }
  return extension;
}

const COVER_ART_ALLOWLIST_LIMIT = 64;

function normalizeRequestedPath(value: unknown): string {
  if (typeof value !== "string") {
    throw new Error("Path must be a string.");
  }

  const trimmed = value.trim();
  if (trimmed.length === 0) {
    throw new Error("Path is required.");
  }
  if (trimmed.includes("\0")) {
    throw new Error("Path contains invalid characters.");
  }
  if (/^[a-z][a-z0-9+.-]*:\/\//i.test(trimmed)) {
    throw new Error("Unsupported path scheme.");
  }
  return normalizePath(trimmed);
}

export class AppController {
  private readonly configStore = new ConfigStore();
  private readonly sessionStore = new SessionStore();
  private readonly browserService = new FileBrowserService();
  private readonly metadataService = new MetadataService();
  private readonly coverArtService = new CoverArtService();
  private readonly lyricsService = new LyricsService();
  private readonly playlistManager = new PlaylistManager();
  private readonly backend = new MpvIpcBackend();
  private readonly eventTarget: EventTarget;

  private settings!: AppSettings;
  private playback: PlaybackSnapshot;
  private status: StatusSnapshot;
  private lyrics: LyricsSnapshot;
  private statusTimer: NodeJS.Timeout | null = null;
  private sessionTimer: NodeJS.Timeout | null = null;
  private currentCoverArtPath: string | null = null;
  private backendReady = false;
  private fileLoadedWaiters: Array<() => void> = [];
  private shutdownPromise: Promise<void> | null = null;
  private readonly metadataQueue: MetadataLoadQueue;
  private shuttingDown = false;
  private playbackEmitTimer: NodeJS.Timeout | null = null;
  private statusEmitTimer: NodeJS.Timeout | null = null;
  private configuredMusicRoot: string | null = null;
  private readonly allowedCoverArtPaths = new Set<string>();
  private liveAudioRuntime: {
    audioCodecName: string | null;
    fileFormat: string | null;
    audioParams: PlaybackAudioParams | null;
    audioOutParams: PlaybackAudioParams | null;
    currentAo: string | null;
    volumeGainDb: number | null;
  } = {
      audioCodecName: null,
      fileFormat: null,
      audioParams: null,
      audioOutParams: null,
      currentAo: null,
      volumeGainDb: null
    };

  public constructor(eventTarget: EventTarget) {
    this.eventTarget = eventTarget;
    this.metadataQueue = new MetadataLoadQueue({
      concurrency: 4,
      runTask: async (task) => {
        await this.executeMetadataLoad(task.trackId, task.filePath);
      }
    });

    this.playback = {
      state: "stopped",
      currentTimeSec: 0,
      durationSec: null,
      volumePercent: 100,
      repeatMode: "off",
      shuffleEnabled: false,
      liveBitrateKbps: null,
      currentTrackId: null
    };

    this.status = {
      temporaryMessage: null,
      backendError: null,
      trackInfo: null
    };

    this.lyrics = {
      path: "",
      lines: [],
      activeIndex: -1,
      visible: false,
      error: null
    };
  }

  public async init(): Promise<InitialState> {
    const defaults = this.configStore.getDefaults();
    this.settings = sanitizeAppSettings(await this.configStore.load(), defaults);
    this.configuredMusicRoot = this.settings.musicRoot;

    if (!(await isDirectory(this.settings.musicRoot))) {
      this.settings.musicRoot = defaults.musicRoot;
    }

    await this.metadataService.loadCache();

    const dependencyReport = await checkRuntimeDependencies();
    if (dependencyReport.missingRequired.length > 0) {
      this.status.backendError = `Missing dependency: ${dependencyReport.missingRequired.join(", ")}. Install it and restart.`;
      this.emitStatus();
      this.eventTarget.emit({
        type: "backend.error",
        payload: { message: this.status.backendError }
      });
    }

    this.backend.subscribe((event) => {
      void this.handleBackendEvent(event);
    });

    if (dependencyReport.missingRequired.length === 0) {
      try {
        await this.backend.start();
        this.backendReady = true;
        await this.backend.setVolume(this.playback.volumePercent);
        await this.backend.setReplayGain(
          this.settings.replaygainPreampTaggedDb,
          this.settings.replaygainPreampUntaggedDb
        );
      } catch (error) {
        this.status.backendError = `Unable to start mpv backend: ${(error as Error).message}`;
        this.emitStatus();
        this.eventTarget.emit({
          type: "backend.error",
          payload: { message: this.status.backendError }
        });
      }
    }

    try {
      await this.restoreSession();
    } catch (error) {
      this.status.backendError = `Session restore failed: ${(error as Error).message}`;
      this.emitStatus();
      this.eventTarget.emit({
        type: "backend.error",
        payload: { message: this.status.backendError }
      });
    }
    this.refreshStatusTrackInfo();

    if (dependencyReport.missingOptional.length > 0) {
      this.setTemporaryStatus(
        `Optional dependency missing: ${dependencyReport.missingOptional.join(", ")} (metadata probing may be limited)`
      );
    }

    this.sessionTimer = setInterval(() => {
      void this.persistSession().catch(() => {
        // welp
      });
    }, 2000);

    return this.getInitialState();
  }

  public async shutdown(): Promise<void> {
    if (this.shutdownPromise) {
      return this.shutdownPromise;
    }

    this.shuttingDown = true;

    this.shutdownPromise = (async () => {
      this.metadataQueue.shutdown();

      if (this.statusTimer) {
        clearTimeout(this.statusTimer);
        this.statusTimer = null;
      }

      if (this.playbackEmitTimer) {
        clearTimeout(this.playbackEmitTimer);
        this.playbackEmitTimer = null;
      }

      if (this.statusEmitTimer) {
        clearTimeout(this.statusEmitTimer);
        this.statusEmitTimer = null;
      }

      if (this.sessionTimer) {
        clearInterval(this.sessionTimer);
        this.sessionTimer = null;
      }

      try {
        await this.persistSession();
      } catch {
        // give up
      }

      try {
        await this.metadataService.persistCache();
      } catch {
        // give up
      }

      if (this.backendReady) {
        try {
          await this.backend.stop();
        } finally {
          this.backendReady = false;
        }
      }
    })();

    await this.shutdownPromise;
  }

  public getInitialState(): InitialState {
    return {
      settings: this.settings,
      playback: this.playback,
      playlist: this.playlistManager.getSnapshot(),
      status: this.status,
      lyrics: this.lyrics
    };
  }

  public getSettings(): AppSettings {
    return this.settings;
  }

  public async saveSettings(next: AppSettings): Promise<{ ok: true } | { ok: false; error: string }> {
    const defaults = this.configStore.getDefaults();
    const normalized = sanitizeAppSettings(next, defaults);

    if (!(await isDirectory(normalized.musicRoot))) {
      return {
        ok: false,
        error: "Music root path does not exist or is not a directory."
      };
    }

    const previousRoot = this.settings.musicRoot;
    this.settings = normalized;
    this.configuredMusicRoot = this.settings.musicRoot;
    await this.configStore.save(this.settings);

    if (this.backendReady) {
      await this.backend.setReplayGain(
        this.settings.replaygainPreampTaggedDb,
        this.settings.replaygainPreampUntaggedDb
      );
    }

    if (previousRoot !== this.settings.musicRoot) {
      this.setTemporaryStatus("Music root updated");
    } else {
      this.setTemporaryStatus("Settings saved");
    }

    await this.metadataService.updatePlrReference(this.settings.plrReferenceLoudnessDb);
    await this.refreshAllPlaylistMetadata();
    this.emitPlaylist();
    this.eventTarget.emit({
      type: "settings.updated",
      payload: this.settings
    });

    return { ok: true };
  }

  public async listBrowserEntries(requestedPath: string): Promise<void> {
    const normalized = normalizeRequestedPath(requestedPath);
    const entries = await this.browserService.listEntries(this.settings.musicRoot, normalized);
    this.eventTarget.emit({
      type: "browser.entries",
      payload: {
        path: normalized,
        entries
      }
    });
  }

  public async getFileProperties(requestedPath: string): Promise<FilePropertiesSnapshot> {
    const normalized = normalizeRequestedPath(requestedPath);

    const stat = await fs.stat(normalized);
    if (!stat.isFile()) {
      throw new Error("Selected path is not a file.");
    }
    if (!isAudioFile(normalized)) {
      throw new Error("Selected file is not a supported audio format.");
    }

    const metadata = await this.metadataService.getMetadata(normalized, this.settings);
    const rawTags: PropertyEntry[] = Object.entries(metadata.tags ?? {}).map(([key, value]) => ({
      key,
      value: stringifyTagValue(value)
    })).sort((a, b) => a.key.localeCompare(b.key));

    const ffprobe = await this.metadataService.getFfprobeEntries(normalized);

    return {
      path: normalized,
      name: path.basename(normalized),
      directory: path.dirname(normalized),
      extension: path.extname(normalized).toLowerCase(),
      stats: {
        sizeBytes: stat.size,
        dev: stat.dev,
        mode: stat.mode,
        nlink: stat.nlink,
        uid: stat.uid,
        gid: stat.gid,
        rdev: stat.rdev,
        blksize: stat.blksize,
        ino: stat.ino,
        blocks: stat.blocks,
        atimeMs: Math.trunc(stat.atimeMs),
        mtimeMs: Math.trunc(stat.mtimeMs),
        ctimeMs: Math.trunc(stat.ctimeMs),
        birthtimeMs: Math.trunc(stat.birthtimeMs)
      },
      metadata,
      rawTags,
      ffprobe
    };
  }

  public async readCoverArtAsDataUrl(requestedPath: string): Promise<string | null> {
    const normalized = normalizeRequestedPath(requestedPath);
    if (!this.allowedCoverArtPaths.has(normalized)) {
      return null;
    }
    return await this.coverArtService.readAsDataUrl(normalized);
  }

  public async handlePlaybackCommand(command: PlaybackCommand): Promise<void> {
    try {
      switch (command.type) {
        case "playPause": {
          if (!this.playback.currentTrackId) {
            const candidate = this.playlistManager.getSelectedTrackId() ?? this.playlistManager.getItems()[0]?.id;
            if (!candidate) {
              this.setTemporaryStatus("Playlist is empty");
              return;
            }
            await this.playTrackById(candidate, true, 0);
            return;
          }

          if (!this.backendReady) {
            this.setTemporaryStatus("Playback backend is unavailable");
            return;
          }

          await this.backend.togglePause();
          return;
        }
        case "play": {
          if (!this.playback.currentTrackId) {
            const candidate = this.playlistManager.getSelectedTrackId() ?? this.playlistManager.getItems()[0]?.id;
            if (!candidate) {
              this.setTemporaryStatus("Playlist is empty");
              return;
            }
            await this.playTrackById(candidate, true, 0);
            return;
          }

          if (!this.backendReady) {
            this.setTemporaryStatus("Playback backend is unavailable");
            return;
          }

          await this.backend.play();
          this.playback.state = "playing";
          this.emitPlayback();
          return;
        }
        case "pause":
          if (!this.backendReady) {
            this.setTemporaryStatus("Playback backend is unavailable");
            return;
          }
          await this.backend.pause();
          this.playback.state = "paused";
          this.emitPlayback();
          return;
        case "playTrack":
          await this.playTrackById(command.trackId, true, 0);
          return;
        case "stop":
          if (this.backendReady && this.playback.currentTrackId) {
            await this.backend.pause();
            try {
              await this.backend.seekAbsolute(0);
            } catch {
              // mpv can reject seek when the file is already unloaded.
            }
          }
          this.playback.state = "stopped";
          this.playback.currentTimeSec = 0;
          this.emitPlayback();
          return;
        case "next":
          await this.playNextTrack();
          return;
        case "previous":
          await this.playPreviousTrack();
          return;
        case "seekRelative":
          await this.backend.seekRelative(command.seconds);
          return;
        case "seekAbsolute":
          await this.backend.seekAbsolute(command.seconds);
          return;
        case "setVolume": {
          if (!this.backendReady) {
            this.setTemporaryStatus("Playback backend is unavailable");
            return;
          }
          const clamped = clamp(Math.round(command.percent), 0, 130);
          await this.backend.setVolume(clamped);
          this.playback.volumePercent = clamped;
          this.emitPlayback();
          return;
        }
        case "setRepeatMode":
          this.playlistManager.setRepeatMode(command.mode);
          this.playback.repeatMode = command.mode;
          this.emitPlayback();
          return;
        case "setShuffle":
          this.playlistManager.setShuffleEnabled(command.enabled);
          this.playback.shuffleEnabled = command.enabled;
          this.emitPlayback();
          return;
        case "cycleRepeat":
          this.playback.repeatMode = this.playlistManager.cycleRepeatMode();
          this.emitPlayback();
          return;
        case "toggleShuffle":
          this.playback.shuffleEnabled = this.playlistManager.toggleShuffle();
          this.emitPlayback();
          return;
        default:
          return;
      }
    } catch (error) {
      const message = (error as Error).message;
      if (message.toLowerCase() === "error running command") {
        this.setTemporaryStatus(message);
        return;
      }

      this.status.backendError = message;
      this.emitStatus();
      this.eventTarget.emit({ type: "backend.error", payload: { message } });
    }
  }

  public async handlePlaylistCommand(command: PlaylistCommand): Promise<void> {
    try {
      switch (command.type) {
        case "addPath": {
          await this.addPathsToPlaylist([command.path]);
          return;
        }
        case "addPaths": {
          await this.addPathsToPlaylist(command.paths, command.index);
          return;
        }
        case "replaceFromPath": {
          await this.replacePlaylistFromPath(command.path, false);
          return;
        }
        case "replaceFromPathAndPlay": {
          await this.replacePlaylistFromPath(command.path, true);
          return;
        }
        case "removeTrack": {
          await this.removeTracksFromPlaylist([command.trackId]);
          return;
        }
        case "removeTracks": {
          await this.removeTracksFromPlaylist(command.trackIds);
          return;
        }
        case "moveTracks": {
          const changed = this.playlistManager.moveTracks(command.trackIds, command.index);
          if (changed) {
            this.emitPlaylist();
          }
          return;
        }
        case "clear":
          if (this.playlistManager.getItems().length === 0) {
            this.setTemporaryStatus("Playlist is empty");
            return;
          }

          this.playlistManager.clear();
          await this.handlePlaybackCommand({ type: "stop" });
          this.playback.currentTrackId = null;
          this.playback.liveBitrateKbps = null;
          this.currentCoverArtPath = null;
          this.lyrics = {
            path: "",
            lines: [],
            activeIndex: -1,
            visible: false,
            error: null
          };
          this.refreshStatusTrackInfo();
          this.emitPlaylist();
          this.emitPlayback();
          this.emitLyrics();
          return;
        case "sort":
          this.playlistManager.sortBy(command.column);
          this.emitPlaylist();
          return;
        case "select": {
          if ("trackIds" in command) {
            this.playlistManager.setSelectedTracks(command.trackIds, command.primaryTrackId);
          } else {
            this.playlistManager.setSelectedTrack(command.trackId);
          }
          this.emitPlaylist();
          return;
        }
        default:
          return;
      }
    } catch (error) {
      this.setTemporaryStatus((error as Error).message || "Playlist command failed");
    }
  }

  private async handleBackendEvent(event: {
    type: "state" | "fileLoaded" | "endOfFile" | "error";
    payload?: Partial<PlaybackBackendState>;
    reason?: string;
    message?: string;
  }): Promise<void> {
    switch (event.type) {
      case "state": {
        const payload = event.payload ?? {};

        if (typeof payload.timePosSec === "number") {
          this.playback.currentTimeSec = payload.timePosSec;
        }

        if (payload.durationSec !== undefined) {
          this.playback.durationSec = payload.durationSec;
        }

        if (typeof payload.volumePercent === "number") {
          this.playback.volumePercent = clamp(Math.round(payload.volumePercent), 0, 130);
        }

        if (payload.audioBitrateKbps !== undefined) {
          this.playback.liveBitrateKbps = payload.audioBitrateKbps;
        }

        if (payload.audioCodecName !== undefined) {
          this.liveAudioRuntime.audioCodecName = payload.audioCodecName;
        }

        if (payload.fileFormat !== undefined) {
          this.liveAudioRuntime.fileFormat = payload.fileFormat;
        }

        if (payload.audioParams !== undefined) {
          this.liveAudioRuntime.audioParams = payload.audioParams;
        }

        if (payload.audioOutParams !== undefined) {
          this.liveAudioRuntime.audioOutParams = payload.audioOutParams;
        }

        if (payload.currentAo !== undefined) {
          this.liveAudioRuntime.currentAo = payload.currentAo;
        }

        if (payload.volumeGainDb !== undefined) {
          this.liveAudioRuntime.volumeGainDb = payload.volumeGainDb;
        }

        if (typeof payload.paused === "boolean") {
          if (this.playback.currentTrackId) {
            this.playback.state = payload.paused ? "paused" : "playing";
          } else {
            this.playback.state = "stopped";
          }
        }

        const updatedLyrics = this.lyricsService.updateActiveIndex(this.lyrics, this.playback.currentTimeSec);
        if (updatedLyrics.activeIndex !== this.lyrics.activeIndex) {
          this.lyrics = updatedLyrics;
          this.emitLyrics();
        }

        this.refreshStatusTrackInfo(false);
        this.scheduleStatusEmit();
        this.schedulePlaybackEmit();
        return;
      }
      case "fileLoaded":
        this.resolveFileLoadedWaiters();
        return;
      case "endOfFile":
        if (event.reason === "eof") {
          await this.playNextTrack(true);
        }
        return;
      case "error":
        this.status.backendError = event.message ?? "Unknown backend error";
        this.emitStatus();
        this.eventTarget.emit({
          type: "backend.error",
          payload: { message: this.status.backendError }
        });
        return;
      default:
        return;
    }
  }

  private async playTrackById(trackId: string, autoplay: boolean, seekToSeconds: number): Promise<void> {
    if (!this.backendReady) {
      this.setTemporaryStatus("Playback backend is unavailable");
      return;
    }

    const track = this.playlistManager.getItemById(trackId);
    if (!track) {
      this.setTemporaryStatus("Selected track not found");
      return;
    }

    this.playlistManager.setCurrentTrack(track.id);
    this.playlistManager.setSelectedTrack(track.id);
    this.playback.currentTrackId = track.id;
    this.clearLiveAudioRuntime();

    await this.backend.setReplayGain(
      this.settings.replaygainPreampTaggedDb,
      this.settings.replaygainPreampUntaggedDb
    );

    await this.backend.loadFile(track.path);
    await this.waitForFileLoaded(1500);
    if (seekToSeconds > 0) {
      try {
        await this.backend.seekAbsolute(seekToSeconds);
      } catch {
        // uhmm
      }
    }

    if (autoplay) {
      await this.backend.play();
      this.playback.state = "playing";
    } else {
      await this.backend.pause();
      this.playback.state = "paused";
    }

    this.playback.currentTimeSec = seekToSeconds;
    this.playback.liveBitrateKbps = null;

    const [lyrics, coverArtPath] = await Promise.all([
      this.lyricsService.loadForTrack(track.path),
      this.coverArtService.resolveForTrack(track.path)
    ]);
    this.lyrics = lyrics;
    this.currentCoverArtPath = coverArtPath;
    this.rememberCoverArtPath(coverArtPath);
    this.refreshStatusTrackInfo();

    this.emitPlaylist();
    this.emitPlayback();
    this.emitLyrics();

    void this.enqueueMetadataLoad(track.id, track.path, "high");
  }

  private async playNextTrack(fromEof = false): Promise<void> {
    const nextTrackId = this.playlistManager.getNextTrackId();

    if (nextTrackId) {
      await this.playTrackById(nextTrackId, true, 0);
      return;
    }

    this.playback.state = "paused";
    this.emitPlayback();

    if (fromEof) {
      this.setTemporaryStatus("End of playlist");
    } else {
      this.setTemporaryStatus("No next track");
    }
  }

  private async playPreviousTrack(): Promise<void> {
    const previousTrackId = this.playlistManager.getPreviousTrackId();
    if (!previousTrackId) {
      this.setTemporaryStatus("No previous track");
      return;
    }

    await this.playTrackById(previousTrackId, true, 0);
  }

  private async addPathsToPlaylist(inputPaths: string[], index?: number): Promise<void> {
    const files = await this.resolveAudioPathsBatch(inputPaths);
    if (files.length === 0) {
      this.setTemporaryStatus("No audio files found");
      return;
    }

    const inserted = this.playlistManager.addPaths(files, index);
    if (inserted.length === 0) {
      this.setTemporaryStatus("Nothing was added to the playlist");
      return;
    }

    this.emitPlaylist();

    for (const [index, item] of inserted.entries()) {
      void this.enqueueMetadataLoad(item.id, item.path, index < 6 ? "high" : "normal");
    }
  }

  private async replacePlaylistFromPath(inputPath: string, playAfterReplace: boolean): Promise<void> {
    await this.handlePlaybackCommand({ type: "stop" });
    const files = await this.resolveAudioPaths(inputPath);
    if (files.length === 0) {
      this.setTemporaryStatus("No audio files found");
      return;
    }

    const inserted = this.playlistManager.replaceWithPaths(files);
    this.playback.currentTrackId = null;
    this.currentCoverArtPath = null;
    this.refreshStatusTrackInfo();
    this.emitPlaylist();
    this.emitPlayback();

    for (const [index, item] of this.playlistManager.getItems().entries()) {
      void this.enqueueMetadataLoad(item.id, item.path, index < 6 ? "high" : "normal");
    }

    if (playAfterReplace) {
      const firstTrackId = inserted[0]?.id;
      if (firstTrackId) {
        await this.playTrackById(firstTrackId, true, 0);
      }
    }
  }

  private async removeTracksFromPlaylist(trackIds: string[]): Promise<void> {
    const uniqueIds = [...new Set(trackIds)];
    const result = this.playlistManager.removeTracks(uniqueIds);
    if (result.removedCount === 0) {
      return;
    }

    this.emitPlaylist();

    if (result.removedCurrent) {
      if (result.nextCurrentTrackId) {
        await this.playTrackById(result.nextCurrentTrackId, true, 0);
      } else {
        await this.handlePlaybackCommand({ type: "stop" });
        this.playback.currentTrackId = null;
        this.playback.liveBitrateKbps = null;
        this.currentCoverArtPath = null;
        this.lyrics = {
          path: "",
          lines: [],
          activeIndex: -1,
          visible: false,
          error: null
        };
        this.refreshStatusTrackInfo();
        this.emitPlayback();
        this.emitLyrics();
      }
    }
  }

  private async resolveAudioPathsBatch(inputPaths: string[]): Promise<string[]> {
    const result: string[] = [];
    const seen = new Set<string>();

    for (const inputPath of inputPaths) {
      const files = await this.resolveAudioPaths(inputPath);
      for (const filePath of files) {
        if (!seen.has(filePath)) {
          seen.add(filePath);
          result.push(filePath);
        }
      }
    }

    return result;
  }

  private async resolveAudioPaths(inputPath: string): Promise<string[]> {
    const normalized = normalizePath(inputPath);

    if (isAudioFile(normalized)) {
      try {
        await fs.access(normalized);
        return [normalized];
      } catch {
        return [];
      }
    }

    if (await isDirectory(normalized)) {
      return await listAudioFilesRecursive(normalized);
    }

    return [];
  }

  private enqueueMetadataLoad(
    trackId: string,
    filePath: string,
    priority: MetadataLoadPriority = "normal"
  ): Promise<void> {
    return this.metadataQueue.enqueue(
      {
        trackId,
        filePath
      },
      priority
    );
  }

  private async executeMetadataLoad(trackId: string, filePath: string): Promise<void> {
    try {
      const preflightTrack = this.playlistManager.getItemById(trackId);
      if (!preflightTrack || preflightTrack.path !== filePath) {
        return;
      }

      const metadata = await this.metadataService.getMetadata(filePath, this.settings);

      const liveTrack = this.playlistManager.getItemById(trackId);
      if (!liveTrack || liveTrack.path !== filePath) {
        return;
      }

      this.playlistManager.updateMetadata(trackId, metadata);

      this.eventTarget.emit({
        type: "playlist.rowMetaUpdated",
        payload: {
          trackId,
          metadata
        }
      });

      if (this.playback.currentTrackId === trackId) {
        this.refreshStatusTrackInfo();
      }
    } catch {
      // give up
    }
  }

  private async refreshAllPlaylistMetadata(): Promise<void> {
    const items = this.playlistManager.getItems();
    await Promise.allSettled(items.map((item, index) =>
      this.enqueueMetadataLoad(
        item.id,
        item.path,
        item.id === this.playback.currentTrackId || index < 4 ? "high" : "normal"
      )));
  }

  private refreshStatusTrackInfo(emit = true): void {
    const currentTrack = this.playback.currentTrackId
      ? this.playlistManager.getItemById(this.playback.currentTrackId)
      : null;

    if (!currentTrack) {
      this.status.trackInfo = null;
      if (emit) {
        this.emitStatus();
      }
      return;
    }

    const metadata = currentTrack.metadata;
    const bitrate =
      this.playback.liveBitrateKbps ?? metadata.technical.bitrateKbps ?? null;

    this.status.trackInfo = {
      displayTitle: metadata.title || currentTrack.path,
      codec: normalizeUpperDisplayValue(this.liveAudioRuntime.audioCodecName ?? metadata.technical.codec),
      container: normalizeUpperDisplayValue(this.liveAudioRuntime.fileFormat ?? inferContainerFromPath(currentTrack.path)),
      sampleRate: formatSampleRate(metadata.technical.sampleRateHz),
      bitDepth: formatBitDepth(metadata.technical.bitDepth, metadata.technical.codec),
      channels: formatChannels(metadata.technical.channels),
      bitrate: bitrate != null ? `${bitrate} kbps` : "unknown",
      outputFormat: "f32",
      coverArtPath: this.currentCoverArtPath
    };

    if (emit) {
      this.emitStatus();
    }
  }

  private clearLiveAudioRuntime(): void {
    this.liveAudioRuntime = {
      audioCodecName: null,
      fileFormat: null,
      audioParams: null,
      audioOutParams: null,
      currentAo: null,
      volumeGainDb: null
    };
  }

  private schedulePlaybackEmit(): void {
    if (this.playbackEmitTimer) {
      return;
    }

    this.playbackEmitTimer = setTimeout(() => {
      this.playbackEmitTimer = null;
      this.emitPlayback();
    }, 120);
  }

  private scheduleStatusEmit(): void {
    if (this.statusEmitTimer) {
      return;
    }

    this.statusEmitTimer = setTimeout(() => {
      this.statusEmitTimer = null;
      this.emitStatus();
    }, 120);
  }

  private setTemporaryStatus(message: string): void {
    this.status.temporaryMessage = message;
    this.eventTarget.emit({ type: "status.message", payload: { message } });
    this.emitStatus();

    if (this.statusTimer) {
      clearTimeout(this.statusTimer);
    }

    this.statusTimer = setTimeout(() => {
      this.status.temporaryMessage = null;
      this.emitStatus();
    }, 3000);
  }

  private async persistSession(): Promise<void> {
    const selectedTrackId = this.playlistManager.getSelectedTrackId();
    const selectedTrack = selectedTrackId
      ? this.playlistManager.getItemById(selectedTrackId)
      : null;
    const currentTrack = this.playback.currentTrackId
      ? this.playlistManager.getItemById(this.playback.currentTrackId)
      : null;

    const session: SessionState = {
      playlistPaths: this.playlistManager.getItems().map((item) => item.path),
      selectedTrackPath: selectedTrack?.path ?? null,
      currentTrackPath: currentTrack?.path ?? null,
      currentTrackPositionSec: this.playback.currentTimeSec,
      repeatMode: this.playback.repeatMode,
      shuffleEnabled: this.playback.shuffleEnabled,
      volumePercent: this.playback.volumePercent,
      musicRoot: this.configuredMusicRoot ?? this.settings.musicRoot
    };

    await this.sessionStore.save(session);
  }

  private async restoreSession(): Promise<void> {
    const session = await this.sessionStore.load();
    if (!session) {
      this.playback.volumePercent = 100;
      return;
    }

    const playlistPaths = Array.isArray(session.playlistPaths) ? session.playlistPaths : [];
    const repeatMode = isValidRepeatMode(session.repeatMode) ? session.repeatMode : "off";
    const shuffleEnabled = Boolean(session.shuffleEnabled);
    const volumePercent = Number.isFinite(session.volumePercent) ? session.volumePercent : 100;
    const currentTrackPositionSec = Number.isFinite(session.currentTrackPositionSec)
      ? session.currentTrackPositionSec
      : 0;

    this.settings.musicRoot = normalizePath(this.configuredMusicRoot ?? this.settings.musicRoot);
    if (!(await isDirectory(this.settings.musicRoot))) {
      this.settings.musicRoot = this.configStore.getDefaults().musicRoot;
    }

    const existingPaths: string[] = [];
    for (const filePath of playlistPaths) {
      const normalized = normalizePath(filePath);
      try {
        await fs.access(normalized);
        if (isAudioFile(normalized)) {
          existingPaths.push(normalized);
        }
      } catch {
        // just ignore it
      }
    }

    const appended = this.playlistManager.addPaths(existingPaths);

    this.playlistManager.setRepeatMode(repeatMode);
    this.playback.repeatMode = repeatMode;
    this.playlistManager.setShuffleEnabled(shuffleEnabled);
    this.playback.shuffleEnabled = shuffleEnabled;
    this.playback.volumePercent = clamp(Math.round(volumePercent), 0, 130);

    const selectedTrackPath = session.selectedTrackPath;
    if (selectedTrackPath) {
      const normalizedSelectedPath = normalizePath(selectedTrackPath);
      const selected = appended.find((item) => item.path === normalizedSelectedPath);
      this.playlistManager.setSelectedTrack(selected?.id ?? null);
    }

    const currentTrackPath = session.currentTrackPath;
    if (this.backendReady && currentTrackPath) {
      const normalizedCurrentPath = normalizePath(currentTrackPath);
      const current = appended.find((item) => item.path === normalizedCurrentPath);
      if (current) {
        await this.playTrackById(current.id, false, Math.max(0, currentTrackPositionSec));
      }
    }

    if (this.backendReady) {
      await this.backend.setVolume(this.playback.volumePercent);
    }

    for (const [index, item] of appended.entries()) {
      const priority: MetadataLoadPriority = (
        item.id === this.playback.currentTrackId || index < 4
      )
        ? "high"
        : "normal";
      void this.enqueueMetadataLoad(item.id, item.path, priority);
    }

    this.emitPlaylist();
    this.emitPlayback();
  }

  private emitPlayback(): void {
    if (this.playbackEmitTimer) {
      clearTimeout(this.playbackEmitTimer);
      this.playbackEmitTimer = null;
    }
    this.eventTarget.emit({ type: "playback.snapshot", payload: this.playback });
  }

  private emitPlaylist(): void {
    this.eventTarget.emit({ type: "playlist.snapshot", payload: this.playlistManager.getSnapshot() });
  }

  private emitStatus(): void {
    if (this.statusEmitTimer) {
      clearTimeout(this.statusEmitTimer);
      this.statusEmitTimer = null;
    }
    this.eventTarget.emit({ type: "status.snapshot", payload: this.status });
  }

  private emitLyrics(): void {
    this.eventTarget.emit({ type: "lyrics.snapshot", payload: this.lyrics });
  }

  private waitForFileLoaded(timeoutMs: number): Promise<void> {
    return new Promise((resolve) => {
      const timer = setTimeout(() => {
        this.fileLoadedWaiters = this.fileLoadedWaiters.filter((waiter) => waiter !== wrappedResolve);
        resolve();
      }, timeoutMs);

      const wrappedResolve = (): void => {
        clearTimeout(timer);
        resolve();
      };

      this.fileLoadedWaiters.push(wrappedResolve);
    });
  }

  private resolveFileLoadedWaiters(): void {
    const waiters = [...this.fileLoadedWaiters];
    this.fileLoadedWaiters = [];
    for (const waiter of waiters) {
      waiter();
    }
  }

  private rememberCoverArtPath(coverArtPath: string | null): void {
    if (!coverArtPath) {
      return;
    }

    const normalized = normalizePath(coverArtPath);
    this.allowedCoverArtPaths.delete(normalized);
    this.allowedCoverArtPaths.add(normalized);

    while (this.allowedCoverArtPaths.size > COVER_ART_ALLOWLIST_LIMIT) {
      const oldest = this.allowedCoverArtPaths.values().next().value;
      if (!oldest) {
        break;
      }
      this.allowedCoverArtPaths.delete(oldest);
    }
  }
}
