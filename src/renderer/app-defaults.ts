import type {
  AppSettings,
  LyricsSnapshot,
  PlaybackSnapshot,
  PlaylistSnapshot,
  StatusSnapshot
} from "../shared/types";

export const LEFT_PANE_MIN_PX = 220;
export const RIGHT_PANE_MIN_PX = 360;
export const DEFAULT_LEFT_PANE_WIDTH = 360;

export const FALLBACK_SETTINGS: AppSettings = {
  musicRoot: ".",
  replaygainPreampTaggedDb: 0,
  replaygainPreampUntaggedDb: 0,
  plrReferenceLoudnessDb: -18,
  volumeStepPercent: 5
};

export const FALLBACK_PLAYBACK: PlaybackSnapshot = {
  state: "stopped",
  currentTimeSec: 0,
  durationSec: null,
  volumePercent: 100,
  repeatMode: "off",
  shuffleEnabled: false,
  liveBitrateKbps: null,
  currentTrackId: null
};

export const FALLBACK_PLAYLIST: PlaylistSnapshot = {
  items: [],
  selectedTrackId: null,
  selectedTrackIds: [],
  currentTrackId: null,
  sortColumn: null,
  sortDirection: "asc"
};

export const FALLBACK_STATUS: StatusSnapshot = {
  temporaryMessage: null,
  backendError: null,
  trackInfo: null
};

export const FALLBACK_LYRICS: LyricsSnapshot = {
  path: "",
  lines: [],
  activeIndex: -1,
  visible: false,
  error: null
};
