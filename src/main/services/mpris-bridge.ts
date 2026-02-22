import path from "node:path";
import { createRequire } from "node:module";
import { pathToFileURL } from "node:url";
import type {
  AppEvent,
  InitialState,
  PlaybackCommand,
  PlaybackSnapshot,
  PlaylistItem,
  PlaylistSnapshot,
  RepeatMode
} from "../../shared/types.js";
import { CoverArtService } from "./cover-art-service.js";

interface MprisPlayer {
  playbackStatus: "Playing" | "Paused" | "Stopped";
  loopStatus: "None" | "Track" | "Playlist";
  shuffle: boolean;
  volume: number;
  metadata: Record<string, unknown>;
  canGoNext: boolean;
  canGoPrevious: boolean;
  canPlay: boolean;
  canPause: boolean;
  canSeek: boolean;
  canControl: boolean;
  getPosition: () => number;
  seeked(positionUs: number): void;
  objectPath(subpath?: string): string;
  on(eventName: string, listener: (...args: unknown[]) => void): void;
  removeAllListeners?(eventName?: string): void;
  _bus?: {
    disconnect?: () => void;
  };
}

type MprisFactory = (options: {
  name: string;
  identity: string;
  desktopEntry?: string;
  supportedUriSchemes?: string[];
  supportedMimeTypes?: string[];
  supportedInterfaces?: string[];
}) => MprisPlayer;

interface MprisBridgeOptions {
  dispatchPlaybackCommand(command: PlaybackCommand): Promise<void>;
  focusMainWindow(): void;
  quitApp(): void;
}

const EMPTY_PLAYBACK: PlaybackSnapshot = {
  state: "stopped",
  currentTimeSec: 0,
  durationSec: null,
  volumePercent: 100,
  repeatMode: "off",
  shuffleEnabled: false,
  liveBitrateKbps: null,
  currentTrackId: null
};

const EMPTY_PLAYLIST: PlaylistSnapshot = {
  items: [],
  selectedTrackId: null,
  selectedTrackIds: [],
  currentTrackId: null,
  sortColumn: null,
  sortDirection: "asc"
};

function toMicroseconds(seconds: number): number {
  return Math.max(0, Math.floor(seconds * 1000000));
}

function repeatModeToLoopStatus(mode: RepeatMode): "None" | "Track" | "Playlist" {
  switch (mode) {
    case "one":
      return "Track";
    case "all":
      return "Playlist";
    default:
      return "None";
  }
}

function loopStatusToRepeatMode(value: unknown): RepeatMode | null {
  if (value === "Track") {
    return "one";
  }
  if (value === "Playlist") {
    return "all";
  }
  if (value === "None") {
    return "off";
  }
  return null;
}

function toFiniteNumber(value: unknown): number | null {
  if (typeof value === "number" && Number.isFinite(value)) {
    return value;
  }
  if (typeof value === "bigint") {
    return Number(value);
  }
  if (typeof value === "string") {
    const parsed = Number(value);
    return Number.isFinite(parsed) ? parsed : null;
  }
  return null;
}

function sanitizePathSegment(input: string): string {
  const sanitized = input.replace(/[^A-Za-z0-9_]/g, "_");
  return sanitized || "track";
}

function readFirstTagValue(
  tags: Record<string, string | number | Array<string | number>> | undefined,
  keys: string[]
): string | null {
  if (!tags) {
    return null;
  }

  for (const key of keys) {
    const value = tags[key];
    if (value == null) {
      continue;
    }

    if (Array.isArray(value)) {
      for (const item of value) {
        const text = String(item).trim();
        if (text) {
          return text;
        }
      }
      continue;
    }

    const text = String(value).trim();
    if (text) {
      return text;
    }
  }

  return null;
}

export class MprisBridge {
  private readonly options: MprisBridgeOptions;
  private readonly coverArtService = new CoverArtService();
  private readonly player: MprisPlayer | null;
  private playback: PlaybackSnapshot = { ...EMPTY_PLAYBACK };
  private playlist: PlaylistSnapshot = { ...EMPTY_PLAYLIST };
  private lastTrackId: string | null = null;
  private lastPositionUs = 0;
  private metadataSyncVersion = 0;

  public constructor(options: MprisBridgeOptions) {
    this.options = options;
    this.player = this.createPlayer();
    if (!this.player) {
      return;
    }

    this.player.getPosition = () => toMicroseconds(this.playback.currentTimeSec);
    this.bindControlEvents();
    this.syncCoreProperties();
    void this.syncTrackMetadata();
  }

  public applyInitialState(initialState: InitialState): void {
    this.playback = initialState.playback;
    this.playlist = initialState.playlist;
    this.syncCoreProperties();
    void this.syncTrackMetadata();
  }

  public handleAppEvent(event: AppEvent): void {
    if (!this.player) {
      return;
    }

    switch (event.type) {
      case "playback.snapshot":
        this.playback = event.payload;
        this.syncCoreProperties();
        this.syncSeekSignal();
        if (this.lastTrackId !== this.playback.currentTrackId) {
          void this.syncTrackMetadata();
        }
        return;
      case "playlist.snapshot":
        this.playlist = event.payload;
        this.syncCoreProperties();
        void this.syncTrackMetadata();
        return;
      case "playlist.rowMetaUpdated":
        this.patchTrackMetadata(event.payload.trackId, event.payload.metadata);
        if (event.payload.trackId === this.playback.currentTrackId) {
          void this.syncTrackMetadata();
        }
        return;
      default:
        return;
    }
  }

  public shutdown(): void {
    if (!this.player) {
      return;
    }

    this.player.removeAllListeners?.();
    this.player._bus?.disconnect?.();
  }

  private createPlayer(): MprisPlayer | null {
    const require = createRequire(import.meta.url);
    let playerFactory: MprisFactory;

    try {
      playerFactory = require("mpris-service") as MprisFactory;
    } catch (error) {
      console.warn("MPRIS disabled: unable to load mpris-service.", error);
      return null;
    }

    const player = playerFactory({
      name: "impulse",
      identity: "Impulse",
      desktopEntry: "impulse",
      supportedUriSchemes: ["file"],
      supportedMimeTypes: ["audio/mpeg", "audio/flac", "audio/ogg", "audio/mp4", "audio/x-wav"],
      supportedInterfaces: ["player"]
    });

    player.on("error", (error) => {
      console.error("MPRIS bridge error:", error);
    });

    return player;
  }

  private bindControlEvents(): void {
    if (!this.player) {
      return;
    }

    this.player.on("raise", () => {
      this.options.focusMainWindow();
    });

    this.player.on("quit", () => {
      this.options.quitApp();
    });

    this.player.on("play", () => {
      void this.options.dispatchPlaybackCommand({ type: "play" });
    });
    this.player.on("pause", () => {
      void this.options.dispatchPlaybackCommand({ type: "pause" });
    });
    this.player.on("playpause", () => {
      void this.options.dispatchPlaybackCommand({ type: "playPause" });
    });
    this.player.on("stop", () => {
      void this.options.dispatchPlaybackCommand({ type: "stop" });
    });
    this.player.on("next", () => {
      void this.options.dispatchPlaybackCommand({ type: "next" });
    });
    this.player.on("previous", () => {
      void this.options.dispatchPlaybackCommand({ type: "previous" });
    });

    this.player.on("seek", (offsetUs: unknown) => {
      const offset = toFiniteNumber(offsetUs);
      if (offset == null) {
        return;
      }
      void this.options.dispatchPlaybackCommand({
        type: "seekRelative",
        seconds: offset / 1000000
      });
    });

    this.player.on("position", (event: unknown) => {
      if (!event || typeof event !== "object") {
        return;
      }

      const entry = event as { trackId?: unknown; position?: unknown };
      const trackId = typeof entry.trackId === "string" ? entry.trackId : null;
      const position = toFiniteNumber(entry.position);

      if (!trackId || position == null) {
        return;
      }

      const currentTrackPath = this.getCurrentTrackObjectPath();
      if (!currentTrackPath || trackId !== currentTrackPath) {
        return;
      }

      void this.options.dispatchPlaybackCommand({
        type: "seekAbsolute",
        seconds: Math.max(0, position / 1000000)
      });
    });

    this.player.on("shuffle", (enabled: unknown) => {
      if (typeof enabled !== "boolean") {
        return;
      }
      void this.options.dispatchPlaybackCommand({ type: "setShuffle", enabled });
    });

    this.player.on("loopStatus", (value: unknown) => {
      const repeatMode = loopStatusToRepeatMode(value);
      if (!repeatMode) {
        return;
      }
      void this.options.dispatchPlaybackCommand({ type: "setRepeatMode", mode: repeatMode });
    });

    this.player.on("volume", (value: unknown) => {
      const volume = toFiniteNumber(value);
      if (volume == null) {
        return;
      }
      void this.options.dispatchPlaybackCommand({
        type: "setVolume",
        percent: Math.max(0, Math.round(volume * 100))
      });
    });
  }

  private syncCoreProperties(): void {
    if (!this.player) {
      return;
    }

    this.player.playbackStatus = this.playback.state === "playing"
      ? "Playing"
      : this.playback.state === "paused"
        ? "Paused"
        : "Stopped";
    this.player.loopStatus = repeatModeToLoopStatus(this.playback.repeatMode);
    this.player.shuffle = this.playback.shuffleEnabled;
    this.player.volume = Math.max(0, this.playback.volumePercent / 100);

    const hasTracks = this.playlist.items.length > 0;
    const hasCurrentTrack = Boolean(this.getCurrentTrack());
    this.player.canControl = true;
    this.player.canPlay = hasTracks;
    this.player.canPause = hasCurrentTrack;
    this.player.canGoNext = hasTracks;
    this.player.canGoPrevious = hasTracks;
    this.player.canSeek = hasCurrentTrack && (this.playback.durationSec ?? 0) > 0;
  }

  private async syncTrackMetadata(): Promise<void> {
    if (!this.player) {
      return;
    }

    const currentTrack = this.getCurrentTrack();
    const syncVersion = ++this.metadataSyncVersion;
    this.lastTrackId = this.playback.currentTrackId;

    if (!currentTrack) {
      this.player.metadata = {
        "mpris:trackid": "/org/mpris/MediaPlayer2/TrackList/NoTrack"
      };
      return;
    }

    const metadata: Record<string, unknown> = {
      "mpris:trackid": this.getTrackObjectPath(currentTrack.id),
      "xesam:url": pathToFileURL(currentTrack.path).toString(),
      "xesam:title": currentTrack.metadata.title || path.basename(currentTrack.path),
      "xesam:album": currentTrack.metadata.album || ""
    };

    const artist = readFirstTagValue(currentTrack.metadata.tags, [
      "artist",
      "artists",
      "albumartist",
      "albumArtist",
      "ARTIST",
      "ALBUMARTIST"
    ]);
    if (artist) {
      metadata["xesam:artist"] = [artist];
    }

    if (currentTrack.metadata.trackNumber != null) {
      metadata["xesam:trackNumber"] = currentTrack.metadata.trackNumber;
    }

    if (currentTrack.metadata.technical.durationSec != null) {
      metadata["mpris:length"] = toMicroseconds(currentTrack.metadata.technical.durationSec);
    }

    this.player.metadata = metadata;

    const coverArtPath = await this.coverArtService.resolveForTrack(currentTrack.path);
    if (syncVersion !== this.metadataSyncVersion) {
      return;
    }

    if (coverArtPath) {
      this.player.metadata = {
        ...metadata,
        "mpris:artUrl": pathToFileURL(coverArtPath).toString()
      };
    }
  }

  private syncSeekSignal(): void {
    if (!this.player || !this.playback.currentTrackId) {
      this.lastPositionUs = toMicroseconds(this.playback.currentTimeSec);
      return;
    }

    const positionUs = toMicroseconds(this.playback.currentTimeSec);
    const deltaUs = Math.abs(positionUs - this.lastPositionUs);

    // Emit Seeked only for discrete jumps to avoid flooding DBus with time-pos updates.
    if (deltaUs > 2000000) {
      this.player.seeked(positionUs);
    }

    this.lastPositionUs = positionUs;
  }

  private patchTrackMetadata(trackId: string, metadata: PlaylistItem["metadata"]): void {
    this.playlist = {
      ...this.playlist,
      items: this.playlist.items.map((item) =>
        item.id === trackId
          ? {
            ...item,
            metadata
          }
          : item
      )
    };
  }

  private getCurrentTrack(): PlaylistItem | null {
    const currentTrackId = this.playback.currentTrackId ?? this.playlist.currentTrackId;
    if (!currentTrackId) {
      return null;
    }

    return this.playlist.items.find((item) => item.id === currentTrackId) ?? null;
  }

  private getCurrentTrackObjectPath(): string | null {
    const currentTrack = this.getCurrentTrack();
    if (!currentTrack) {
      return null;
    }

    return this.getTrackObjectPath(currentTrack.id);
  }

  private getTrackObjectPath(trackId: string): string {
    if (!this.player) {
      return "/org/mpris/MediaPlayer2/TrackList/NoTrack";
    }

    return this.player.objectPath(`track/${sanitizePathSegment(trackId)}`);
  }
}
