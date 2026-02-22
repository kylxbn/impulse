import path from "node:path";
import { clamp } from "../../shared/format.js";
import type { AppSettings } from "../../shared/types.js";

function asFiniteNumber(value: unknown, fallback: number): number {
  if (typeof value === "number" && Number.isFinite(value)) {
    return value;
  }

  if (typeof value === "string") {
    const parsed = Number(value);
    if (Number.isFinite(parsed)) {
      return parsed;
    }
  }

  return fallback;
}

function asNormalizedPath(value: unknown, fallback: string): string {
  if (typeof value === "string" && value.trim().length > 0) {
    return path.resolve(value);
  }

  return path.resolve(fallback);
}

function roundToTenth(value: number): number {
  return Math.round(value * 10) / 10;
}

export function sanitizeAppSettings(candidate: Partial<AppSettings>, defaults: AppSettings): AppSettings {
  const volumeStep = asFiniteNumber(candidate.volumeStepPercent, defaults.volumeStepPercent);

  return {
    musicRoot: asNormalizedPath(candidate.musicRoot, defaults.musicRoot),
    replaygainPreampTaggedDb: roundToTenth(
      asFiniteNumber(candidate.replaygainPreampTaggedDb, defaults.replaygainPreampTaggedDb)
    ),
    replaygainPreampUntaggedDb: roundToTenth(
      asFiniteNumber(candidate.replaygainPreampUntaggedDb, defaults.replaygainPreampUntaggedDb)
    ),
    plrReferenceLoudnessDb: roundToTenth(
      asFiniteNumber(candidate.plrReferenceLoudnessDb, defaults.plrReferenceLoudnessDb)
    ),
    volumeStepPercent: clamp(Math.round(volumeStep), 1, 50)
  };
}
