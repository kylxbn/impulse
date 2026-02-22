import path from "node:path";
import { promises as fs } from "node:fs";
import { execFile } from "node:child_process";
import { promisify } from "node:util";
import { app } from "electron";
import { parseFile } from "music-metadata";
import type { AppSettings, PropertyEntry, ReplayGainInfo, TrackMetadata } from "../../shared/types.js";

const execFileAsync = promisify(execFile);
const CACHE_FILE = "metadata-cache.json";

interface ProbeFormat {
  duration?: number;
  bit_rate?: string;
  tags?: Record<string, string>;
}

interface ProbeStream {
  codec_name?: string;
  sample_rate?: string;
  channels?: number;
  bits_per_sample?: number;
  bits_per_raw_sample?: number;
  bit_rate?: string;
  tags?: Record<string, string>;
}

interface ProbeOutput {
  format?: ProbeFormat;
  streams?: ProbeStream[];
}

interface ProbeRawOutput {
  format?: Record<string, unknown>;
  streams?: Array<Record<string, unknown>>;
}

interface FileFingerprint {
  sizeBytes: number;
  mtimeMs: number;
}

interface MetadataCacheEntry {
  metadata: TrackMetadata;
  fileFingerprint: FileFingerprint | null;
}

interface PersistedMetadataCacheV2 {
  version: 2;
  entries: Record<string, MetadataCacheEntry>;
}

function normalizeTagKey(tag: string): string {
  return tag.toLowerCase().replace(/[^a-z0-9]/g, "");
}

function parseMaybeDb(input: unknown): number | null {
  if (typeof input === "number" && Number.isFinite(input)) {
    return input;
  }

  if (typeof input !== "string") {
    return null;
  }

  const value = input.replace("dB", "").trim();
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : null;
}

function parseNumber(input: unknown): number | null {
  if (typeof input === "number" && Number.isFinite(input)) {
    return input;
  }

  if (typeof input === "string") {
    const parsed = Number(input);
    if (Number.isFinite(parsed)) {
      return parsed;
    }
  }

  return null;
}

function parsePositiveInteger(input: unknown): number | null {
  const parsed = parseNumber(input);
  if (parsed == null || parsed <= 0) {
    return null;
  }

  return Math.round(parsed);
}

function linearPeakToDb(linear: number | null): number | null {
  if (linear == null || linear <= 0) {
    return null;
  }
  return 20 * Math.log10(linear);
}

function extractReplayGain(tags: Record<string, unknown>): ReplayGainInfo {
  const normalized = new Map<string, unknown>();
  for (const [key, value] of Object.entries(tags)) {
    normalized.set(normalizeTagKey(key), value);
  }

  const trackGainDb = parseMaybeDb(normalized.get("replaygaintrackgain"));

  const peakLinear = parseNumber(normalized.get("replaygaintrackpeak"));
  const peakDbFromTag = parseMaybeDb(normalized.get("replaygaintrackpeakdb"));
  const trackPeakDb = peakDbFromTag ?? linearPeakToDb(peakLinear);

  return {
    trackGainDb,
    trackPeakLinear: peakLinear,
    trackPeakDb
  };
}

function parseTrackNumber(value: unknown): number | null {
  if (typeof value === "number") {
    return Number.isFinite(value) ? Math.floor(value) : null;
  }

  if (typeof value !== "string") {
    return null;
  }

  const token = value.split("/")[0]?.trim();
  if (!token) {
    return null;
  }

  const parsed = Number(token);
  return Number.isFinite(parsed) ? Math.floor(parsed) : null;
}

function pickTag(tags: Record<string, unknown>, candidates: string[]): unknown {
  const normalizedLookup = new Map<string, unknown>();
  for (const [key, value] of Object.entries(tags)) {
    normalizedLookup.set(normalizeTagKey(key), value);
  }

  for (const candidate of candidates) {
    const value = normalizedLookup.get(normalizeTagKey(candidate));
    if (value != null) {
      return value;
    }
  }

  return null;
}

function computePlr(peakDb: number | null, gainDb: number | null, referenceDb: number): number | null {
  if (peakDb == null || gainDb == null) {
    return null;
  }

  // The formula I used is in `https://wiki.hydrogenaudio.org/index.php?title=Foobar2000:Titleformat_Examples#Peak_to_Loudness_Ratio_(PLR)`
  // Basically, ReplayGain loudness relation: gain = reference - track_loudness
  // so that means track_loudness = reference - gain

  const estimatedLoudness = referenceDb - gainDb;
  const plr = peakDb - estimatedLoudness;
  return Number.isFinite(plr) ? Math.round(plr) : null;
}

function isFingerprintEqual(a: FileFingerprint | null, b: FileFingerprint | null): boolean {
  if (!a || !b) {
    return false;
  }

  return a.sizeBytes === b.sizeBytes && a.mtimeMs === b.mtimeMs;
}

function toMetadataCacheEntry(candidate: unknown): MetadataCacheEntry | null {
  if (!candidate || typeof candidate !== "object") {
    return null;
  }

  const objectValue = candidate as Record<string, unknown>;
  const metadata = objectValue.metadata as TrackMetadata;
  const fingerprint = objectValue.fileFingerprint as {
    sizeBytes?: unknown;
    mtimeMs?: unknown;
  } | null;

  if (
    fingerprint
    && typeof fingerprint.sizeBytes === "number"
    && Number.isFinite(fingerprint.sizeBytes)
    && typeof fingerprint.mtimeMs === "number"
    && Number.isFinite(fingerprint.mtimeMs)
  ) {
    return {
      metadata,
      fileFingerprint: {
        sizeBytes: fingerprint.sizeBytes,
        mtimeMs: fingerprint.mtimeMs
      }
    };
  }

  return {
    metadata,
    fileFingerprint: null
  };
}

function appendProbeEntries(target: PropertyEntry[], prefix: string, value: unknown): void {
  if (value == null) {
    return;
  }

  if (typeof value === "string" || typeof value === "number" || typeof value === "boolean") {
    target.push({
      key: prefix,
      value: String(value)
    });
    return;
  }

  if (Array.isArray(value)) {
    for (const [index, entry] of value.entries()) {
      appendProbeEntries(target, `${prefix}[${index}]`, entry);
    }
    return;
  }

  if (typeof value === "object") {
    for (const [key, entry] of Object.entries(value as Record<string, unknown>)) {
      const nextPrefix = prefix ? `${prefix}.${key}` : key;
      appendProbeEntries(target, nextPrefix, entry);
    }
  }
}

export class MetadataService {
  private readonly cache = new Map<string, MetadataCacheEntry>();
  private readonly cachePath: string;
  private readonly inFlight = new Map<string, Promise<TrackMetadata>>();

  public constructor() {
    this.cachePath = path.join(app.getPath("userData"), CACHE_FILE);
  }

  public async loadCache(): Promise<void> {
    try {
      const payload = await fs.readFile(this.cachePath, "utf8");
      const parsed = JSON.parse(payload) as unknown;

      if (
        parsed
        && typeof parsed === "object"
        && (parsed as { version?: unknown }).version === 2
        && (parsed as { entries?: unknown }).entries
      ) {
        const entries = (parsed as PersistedMetadataCacheV2).entries;
        for (const [key, value] of Object.entries(entries)) {
          const normalizedEntry = toMetadataCacheEntry(value);
          if (normalizedEntry) {
            this.cache.set(key, normalizedEntry);
          }
        }
        return;
      }

      for (const [key, value] of Object.entries(parsed as Record<string, unknown>)) {
        const normalizedEntry = toMetadataCacheEntry(value);
        if (normalizedEntry) {
          this.cache.set(key, normalizedEntry);
        }
      }
    } catch {
      // ignore
    }
  }

  public async persistCache(): Promise<void> {
    const serialized: PersistedMetadataCacheV2 = {
      version: 2,
      entries: {}
    };

    for (const [key, value] of this.cache.entries()) {
      serialized.entries[key] = value;
    }

    await fs.mkdir(path.dirname(this.cachePath), { recursive: true });
    await fs.writeFile(this.cachePath, JSON.stringify(serialized), "utf8");
  }

  public async getMetadata(filePath: string, settings: AppSettings): Promise<TrackMetadata> {
    const fingerprint = await this.getFileFingerprint(filePath);
    const cached = this.cache.get(filePath);
    if (cached) {
      if (cached.fileFingerprint === null || isFingerprintEqual(cached.fileFingerprint, fingerprint)) {
        return cached.metadata;
      }
    }

    const pending = this.inFlight.get(filePath);
    if (pending) {
      return await pending;
    }

    const loader = (async () => {
      const fromProbe = await this.probeWithFfprobe(filePath);
      const shouldUseMusicMetadata = (
        fromProbe.durationSec == null
        || fromProbe.codec == null
        || Object.keys(fromProbe.tags).length === 0
      );
      const fromTags = shouldUseMusicMetadata
        ? await this.probeWithMusicMetadata(filePath, fromProbe.durationSec == null)
        : {
          durationSec: null,
          codec: null,
          bitrateKbps: null,
          sampleRateHz: null,
          bitDepth: null,
          channels: null,
          album: "",
          title: path.basename(filePath),
          tags: {}
        };

      const mergedTags = {
        ...(fromProbe.tags ?? {}),
        ...(fromTags.tags ?? {})
      };

      const replaygain = extractReplayGain(mergedTags);

      const metadata: TrackMetadata = {
        trackNumber: parseTrackNumber(pickTag(mergedTags, ["track", "tracknumber"])),
        album: String(pickTag(mergedTags, ["album"]) ?? fromTags.album ?? ""),
        title: String(pickTag(mergedTags, ["title"]) ?? fromTags.title ?? path.basename(filePath)),
        technical: {
          durationSec: fromProbe.durationSec ?? fromTags.durationSec,
          codec: fromProbe.codec ?? fromTags.codec,
          bitrateKbps: fromProbe.bitrateKbps ?? fromTags.bitrateKbps,
          sampleRateHz: fromProbe.sampleRateHz ?? fromTags.sampleRateHz,
          bitDepth: fromProbe.bitDepth ?? fromTags.bitDepth,
          channels: fromProbe.channels ?? fromTags.channels
        },
        replaygain,
        plr: computePlr(replaygain.trackPeakDb, replaygain.trackGainDb, settings.plrReferenceLoudnessDb),
        tags: mergedTags
      };

      this.cache.set(filePath, {
        metadata,
        fileFingerprint: fingerprint
      });
      return metadata;
    })();

    this.inFlight.set(filePath, loader);
    try {
      return await loader;
    } finally {
      this.inFlight.delete(filePath);
    }
  }

  public async updatePlrReference(referenceDb: number): Promise<void> {
    for (const entry of this.cache.values()) {
      entry.metadata.plr = computePlr(
        entry.metadata.replaygain.trackPeakDb,
        entry.metadata.replaygain.trackGainDb,
        referenceDb
      );
    }
  }

  public async getFfprobeEntries(filePath: string): Promise<PropertyEntry[]> {
    try {
      const { stdout } = await execFileAsync("ffprobe", [
        "-v",
        "error",
        "-show_format",
        "-show_streams",
        "-of",
        "json",
        filePath
      ], {
        timeout: 8000
      });

      const parsed = JSON.parse(stdout) as ProbeRawOutput;
      const entries: PropertyEntry[] = [];

      if (parsed.format) {
        appendProbeEntries(entries, "ffprobe.format", parsed.format);
      }

      if (Array.isArray(parsed.streams)) {
        for (const [streamIndex, stream] of parsed.streams.entries()) {
          appendProbeEntries(entries, `ffprobe.streams[${streamIndex}]`, stream);
        }
      }

      entries.sort((a, b) => a.key.localeCompare(b.key));
      return entries;
    } catch {
      return [];
    }
  }

  private async getFileFingerprint(filePath: string): Promise<FileFingerprint | null> {
    try {
      const stat = await fs.stat(filePath);
      return {
        sizeBytes: stat.size,
        mtimeMs: Math.trunc(stat.mtimeMs)
      };
    } catch {
      return null;
    }
  }

  private async probeWithFfprobe(filePath: string): Promise<{
    durationSec: number | null;
    codec: string | null;
    bitrateKbps: number | null;
    sampleRateHz: number | null;
    bitDepth: number | null;
    channels: number | null;
    tags: Record<string, string>;
  }> {
    try {
      const { stdout } = await execFileAsync("ffprobe", [
        "-v",
        "error",
        "-select_streams",
        "a:0",
        "-show_entries",
        "format=duration,bit_rate:format_tags:stream=codec_name,sample_rate,channels,bits_per_sample,bits_per_raw_sample,bit_rate:stream_tags",
        "-of",
        "json",
        filePath
      ], {
        timeout: 8000
      });

      const parsed = JSON.parse(stdout) as ProbeOutput;
      const audioStream = parsed.streams?.find((stream) => Boolean(stream.codec_name)) ?? null;

      const tags = {
        ...(parsed.format?.tags ?? {}),
        ...(audioStream?.tags ?? {})
      };

      return {
        durationSec: parseNumber(parsed.format?.duration),
        codec: audioStream?.codec_name?.toUpperCase() ?? null,
        bitrateKbps: Math.round((parseNumber(audioStream?.bit_rate) ?? parseNumber(parsed.format?.bit_rate) ?? 0) / 1000) || null,
        sampleRateHz: parsePositiveInteger(audioStream?.sample_rate),
        bitDepth:
          parsePositiveInteger(audioStream?.bits_per_raw_sample)
          ?? parsePositiveInteger(audioStream?.bits_per_sample),
        channels: parsePositiveInteger(audioStream?.channels),
        tags
      };
    } catch {
      return {
        durationSec: null,
        codec: null,
        bitrateKbps: null,
        sampleRateHz: null,
        bitDepth: null,
        channels: null,
        tags: {}
      };
    }
  }

  private async probeWithMusicMetadata(filePath: string, includeDuration: boolean): Promise<{
    durationSec: number | null;
    codec: string | null;
    bitrateKbps: number | null;
    sampleRateHz: number | null;
    bitDepth: number | null;
    channels: number | null;
    album: string;
    title: string;
    tags: Record<string, string | number>;
  }> {
    try {
      const parsed = await parseFile(filePath, {
        skipCovers: true,
        duration: includeDuration
      });
      const tags: Record<string, string | number> = {};

      for (const [key, value] of Object.entries(parsed.common)) {
        if (typeof value === "string" || typeof value === "number") {
          tags[key] = value;
          continue;
        }

        if (Array.isArray(value)) {
          const flattened = value
            .filter((entry): entry is string | number => typeof entry === "string" || typeof entry === "number")
            .map((entry) => String(entry))
            .join(", ");
          if (flattened.length > 0) {
            tags[key] = flattened;
          }
        }
      }

      if (parsed.common.track.no) {
        tags.track = parsed.common.track.no;
      }

      return {
        durationSec: parsed.format.duration ?? null,
        codec: parsed.format.codec?.toUpperCase() ?? null,
        bitrateKbps: parsed.format.bitrate ? Math.round(parsed.format.bitrate / 1000) : null,
        sampleRateHz: parsePositiveInteger(parsed.format.sampleRate),
        bitDepth: parsePositiveInteger(parsed.format.bitsPerSample),
        channels: parsePositiveInteger(parsed.format.numberOfChannels),
        album: parsed.common.album ?? "",
        title: parsed.common.title ?? path.basename(filePath),
        tags
      };
    } catch {
      return {
        durationSec: null,
        codec: null,
        bitrateKbps: null,
        sampleRateHz: null,
        bitDepth: null,
        channels: null,
        album: "",
        title: path.basename(filePath),
        tags: {}
      };
    }
  }
}
