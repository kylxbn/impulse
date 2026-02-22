export const SUPPORTED_AUDIO_EXTENSIONS = new Set([
  ".aac",
  ".aif",
  ".aiff",
  ".alac",
  ".ape",
  ".flac",
  ".m4a",
  ".mka",
  ".mp3",
  ".ogg",
  ".opus",
  ".wav",
  ".wv",
  ".wma"
]);

export const APP_NAME = "Impulse";

export const DEFAULT_SETTINGS = {
  musicRoot: ".",
  replaygainPreampTaggedDb: 0,
  replaygainPreampUntaggedDb: 0,
  plrReferenceLoudnessDb: -18,
  volumeStepPercent: 5
} as const;
