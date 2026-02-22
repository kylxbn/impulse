import path from "node:path";
import { promises as fs } from "node:fs";
import type { LyricsSnapshot, LyricLine } from "../../shared/types.js";

const TIMESTAMP_REGEX = /\[(\d{1,2}):(\d{2})(?:[.:](\d{1,3}))?\]/g;
const OFFSET_REGEX = /^\[offset:([+-]?\d+)\]$/i;

function timestampToSeconds(minutes: string, seconds: string, fraction?: string): number {
  const minuteValue = Number(minutes);
  const secondValue = Number(seconds);
  const fractionValue = fraction ? Number(`0.${fraction.padEnd(3, "0")}`) : 0;
  return Math.max(0, minuteValue * 60 + secondValue + fractionValue);
}

export class LyricsService {
  public async loadForTrack(trackPath: string): Promise<LyricsSnapshot> {
    const lrcPath = this.getLrcPath(trackPath);

    try {
      const contents = await fs.readFile(lrcPath, "utf8");
      const parsed = this.parseLrc(contents);
      if (parsed.length === 0) {
        return {
          path: lrcPath,
          lines: [],
          activeIndex: -1,
          visible: false,
          error: null
        };
      }

      return {
        path: lrcPath,
        lines: parsed,
        activeIndex: -1,
        visible: true,
        error: null
      };
    } catch (error) {
      if ((error as NodeJS.ErrnoException).code === "ENOENT") {
        return {
          path: lrcPath,
          lines: [],
          activeIndex: -1,
          visible: false,
          error: null
        };
      }

      return {
        path: lrcPath,
        lines: [],
        activeIndex: -1,
        visible: false,
        error: `Failed to read lyrics: ${(error as Error).message}`
      };
    }
  }

  public updateActiveIndex(snapshot: LyricsSnapshot, positionSec: number): LyricsSnapshot {
    if (!snapshot.visible || snapshot.lines.length === 0) {
      return {
        ...snapshot,
        activeIndex: -1
      };
    }

    let low = 0;
    let high = snapshot.lines.length - 1;
    let best = -1;

    while (low <= high) {
      const mid = Math.floor((low + high) / 2);
      const current = snapshot.lines[mid];
      if (!current) {
        break;
      }

      if (current.timestampSec <= positionSec) {
        best = mid;
        low = mid + 1;
      } else {
        high = mid - 1;
      }
    }

    return {
      ...snapshot,
      activeIndex: best
    };
  }

  private getLrcPath(trackPath: string): string {
    const ext = path.extname(trackPath);
    return trackPath.slice(0, trackPath.length - ext.length) + ".lrc";
  }

  private parseLrc(contents: string): LyricLine[] {
    const lines = contents.split(/\r?\n/);
    const output: LyricLine[] = [];
    let globalOffsetMs = 0;

    for (const rawLine of lines) {
      const line = rawLine.trim();
      if (!line) {
        continue;
      }

      const offsetMatch = line.match(OFFSET_REGEX);
      if (offsetMatch?.[1]) {
        globalOffsetMs = Number(offsetMatch[1]);
        continue;
      }

      const timestamps: number[] = [];
      let match: RegExpExecArray | null;
      TIMESTAMP_REGEX.lastIndex = 0;
      while ((match = TIMESTAMP_REGEX.exec(line)) != null) {
        timestamps.push(timestampToSeconds(match[1] ?? "0", match[2] ?? "0", match[3]));
      }

      if (timestamps.length === 0) {
        continue;
      }

      const text = line.replace(TIMESTAMP_REGEX, "").trim();
      if (!text) {
        continue;
      }

      for (const timestamp of timestamps) {
        const adjusted = Math.max(0, timestamp + globalOffsetMs / 1000);
        output.push({ timestampSec: adjusted, text });
      }
    }

    output.sort((a, b) => a.timestampSec - b.timestampSec);
    return output;
  }
}
