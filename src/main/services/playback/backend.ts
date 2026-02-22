export interface PlaybackAudioParams {
  format: string | null;
  sampleRateHz: number | null;
  channels: string | null;
  channelCount: number | null;
}

export interface PlaybackBackendState {
  paused: boolean;
  timePosSec: number;
  durationSec: number | null;
  volumePercent: number;
  audioBitrateKbps: number | null;
  audioCodecName: string | null;
  fileFormat: string | null;
  audioParams: PlaybackAudioParams | null;
  audioOutParams: PlaybackAudioParams | null;
  currentAo: string | null;
  volumeGainDb: number | null;
}

export type PlaybackBackendEvent =
  | { type: "state"; payload: Partial<PlaybackBackendState> }
  | { type: "fileLoaded" }
  | { type: "endOfFile"; reason: string }
  | { type: "error"; message: string };

export interface PlaybackBackend {
  start(): Promise<void>;
  stop(): Promise<void>;
  loadFile(filePath: string): Promise<void>;
  play(): Promise<void>;
  pause(): Promise<void>;
  togglePause(): Promise<void>;
  seekRelative(seconds: number): Promise<void>;
  seekAbsolute(seconds: number): Promise<void>;
  setVolume(percent: number): Promise<void>;
  setReplayGain(taggedDb: number, untaggedDb: number): Promise<void>;
  subscribe(listener: (event: PlaybackBackendEvent) => void): () => void;
}
