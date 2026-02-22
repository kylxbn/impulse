export type RepeatMode = "off" | "all" | "one";

export type PlaybackState = "stopped" | "playing" | "paused";

export interface AppSettings {
  musicRoot: string;
  replaygainPreampTaggedDb: number;
  replaygainPreampUntaggedDb: number;
  plrReferenceLoudnessDb: number;
  volumeStepPercent: number;
}

export interface ReplayGainInfo {
  trackGainDb: number | null;
  trackPeakLinear: number | null;
  trackPeakDb: number | null;
}

export interface TechnicalInfo {
  durationSec: number | null;
  codec: string | null;
  bitrateKbps: number | null;
  sampleRateHz: number | null;
  bitDepth: number | null;
  channels: number | null;
}

export interface TrackMetadata {
  trackNumber: number | null;
  album: string;
  title: string;
  technical: TechnicalInfo;
  replaygain: ReplayGainInfo;
  plr: number | null;
  tags?: Record<string, string | number | Array<string | number>>;
}

export interface PropertyEntry {
  key: string;
  value: string;
}

export interface FileStatsSnapshot {
  sizeBytes: number;
  dev: number;
  mode: number;
  nlink: number;
  uid: number;
  gid: number;
  rdev: number;
  blksize: number;
  ino: number;
  blocks: number;
  atimeMs: number;
  mtimeMs: number;
  ctimeMs: number;
  birthtimeMs: number;
}

export interface FilePropertiesSnapshot {
  path: string;
  name: string;
  directory: string;
  extension: string;
  stats: FileStatsSnapshot;
  metadata: TrackMetadata;
  rawTags: PropertyEntry[];
  ffprobe: PropertyEntry[];
}

export interface PlaylistItem {
  id: string;
  path: string;
  metadata: TrackMetadata;
}

export type PlaylistSortColumn =
  | "track"
  | "album"
  | "title"
  | "length"
  | "codec"
  | "bitrate"
  | "replaygain"
  | "plr";

export type SortDirection = "asc" | "desc";

export interface PlaybackSnapshot {
  state: PlaybackState;
  currentTimeSec: number;
  durationSec: number | null;
  volumePercent: number;
  repeatMode: RepeatMode;
  shuffleEnabled: boolean;
  liveBitrateKbps: number | null;
  currentTrackId: string | null;
}

export interface PlaylistSnapshot {
  items: PlaylistItem[];
  selectedTrackId: string | null;
  selectedTrackIds: string[];
  currentTrackId: string | null;
  sortColumn: PlaylistSortColumn | null;
  sortDirection: SortDirection;
}

export interface FileBrowserNode {
  path: string;
  name: string;
  type: "directory" | "file";
  hasChildren: boolean;
}

export interface LyricLine {
  timestampSec: number;
  text: string;
}

export interface LyricsSnapshot {
  path: string;
  lines: LyricLine[];
  activeIndex: number;
  visible: boolean;
  error: string | null;
}

export interface StatusSnapshot {
  temporaryMessage: string | null;
  backendError: string | null;
  trackInfo: {
    displayTitle: string;
    codec: string;
    container: string;
    sampleRate: string;
    bitDepth: string;
    channels: string;
    bitrate: string;
    outputFormat: string;
    coverArtPath: string | null;
  } | null;
}

export interface SessionState {
  playlistPaths: string[];
  selectedTrackPath: string | null;
  currentTrackPath: string | null;
  currentTrackPositionSec: number;
  repeatMode: RepeatMode;
  shuffleEnabled: boolean;
  volumePercent: number;
  musicRoot: string;
}

export type AppEvent =
  | { type: "playback.snapshot"; payload: PlaybackSnapshot }
  | { type: "playlist.snapshot"; payload: PlaylistSnapshot }
  | { type: "playlist.rowMetaUpdated"; payload: { trackId: string; metadata: TrackMetadata } }
  | { type: "browser.entries"; payload: { path: string; entries: FileBrowserNode[] } }
  | { type: "settings.updated"; payload: AppSettings }
  | { type: "lyrics.snapshot"; payload: LyricsSnapshot }
  | { type: "status.snapshot"; payload: StatusSnapshot }
  | { type: "status.message"; payload: { message: string } }
  | { type: "backend.error"; payload: { message: string } };

export type PlaybackCommand =
  | { type: "playPause" }
  | { type: "play" }
  | { type: "pause" }
  | { type: "playTrack"; trackId: string }
  | { type: "stop" }
  | { type: "next" }
  | { type: "previous" }
  | { type: "seekRelative"; seconds: number }
  | { type: "seekAbsolute"; seconds: number }
  | { type: "setVolume"; percent: number }
  | { type: "setRepeatMode"; mode: RepeatMode }
  | { type: "setShuffle"; enabled: boolean }
  | { type: "cycleRepeat" }
  | { type: "toggleShuffle" };

export type PlaylistCommand =
  | { type: "addPath"; path: string }
  | { type: "addPaths"; paths: string[]; index?: number }
  | { type: "replaceFromPath"; path: string }
  | { type: "replaceFromPathAndPlay"; path: string }
  | { type: "removeTrack"; trackId: string }
  | { type: "removeTracks"; trackIds: string[] }
  | { type: "moveTracks"; trackIds: string[]; index: number }
  | { type: "clear" }
  | { type: "sort"; column: PlaylistSortColumn }
  | { type: "select"; trackId: string | null }
  | { type: "select"; trackIds: string[]; primaryTrackId: string | null };

export interface InitialState {
  settings: AppSettings;
  playback: PlaybackSnapshot;
  playlist: PlaylistSnapshot;
  status: StatusSnapshot;
  lyrics: LyricsSnapshot;
}

export type AppMenuAction =
  | "add-selected-path"
  | "replace-selected-path"
  | "open-settings"
  | "remove-selected-track"
  | "clear-playlist"
  | "focus-browser"
  | "focus-playlist"
  | "sort-track"
  | "sort-album"
  | "sort-title"
  | "sort-length"
  | "sort-codec"
  | "sort-bitrate"
  | "sort-replaygain"
  | "sort-plr";
