export function formatDuration(totalSeconds: number | null): string {
  if (totalSeconds == null || !Number.isFinite(totalSeconds) || totalSeconds < 0) {
    return "--:--";
  }

  const seconds = Math.floor(totalSeconds);
  const hours = Math.floor(seconds / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  const remaining = seconds % 60;

  if (hours > 0) {
    return `${hours}:${String(minutes).padStart(2, "0")}:${String(remaining).padStart(2, "0")}`;
  }

  return `${String(minutes).padStart(2, "0")}:${String(remaining).padStart(2, "0")}`;
}

export function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
}

export function formatSignedDb(value: number | null): string {
  if (value == null || !Number.isFinite(value)) {
    return "-";
  }
  return `${value >= 0 ? "+" : ""}${value.toFixed(2)}`;
}

export function formatSampleRate(hz: number | null): string {
  if (hz == null || hz <= 0) {
    return "unknown";
  }
  return `${(hz / 1000).toFixed(1)} kHz`;
}

function normalizeCodec(codec: string): string {
  return codec.trim().toUpperCase().replace(/[^A-Z0-9]/g, "");
}

const LOSSY_CODEC_HINTS = new Set([
  "AAC",
  "AACLATM",
  "MP1",
  "MP2",
  "MP3",
  "MPEG1LAYER3",
  "MPEG2LAYER3",
  "VORBIS",
  "OPUS",
  "WMA",
  "WMAV1",
  "WMAV2",
  "WMAPRO",
  "AC3",
  "EAC3",
  "AMRNB",
  "AMRWB",
  "SPEEX",
  "ATRAC3",
  "ATRAC3P",
  "ATRAC9"
]);

function isLikelyLossyCodec(codec: string | null): boolean {
  if (!codec) {
    return false;
  }

  const normalized = normalizeCodec(codec);
  if (!normalized) {
    return false;
  }

  if (LOSSY_CODEC_HINTS.has(normalized)) {
    return true;
  }

  return normalized.startsWith("WMA");
}

export function formatBitDepth(depth: number | null, codec: string | null = null): string {
  if (depth == null || depth <= 0) {
    if (isLikelyLossyCodec(codec)) {
      return "lossy";
    }
    return "unknown";
  }
  return `${depth}-bit`;
}

export function formatChannels(channels: number | null): string {
  if (channels == null || channels <= 0) {
    return "unknown";
  }
  if (channels === 1) {
    return "mono";
  }
  if (channels === 2) {
    return "stereo";
  }
  return `${channels} ch`;
}
