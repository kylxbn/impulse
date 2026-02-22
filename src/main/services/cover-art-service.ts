import path from "node:path";
import { promises as fs, type Dirent } from "node:fs";

const SUPPORTED_IMAGE_EXTENSIONS = new Set([
  ".jpg",
  ".jpeg",
  ".png",
  ".webp",
  ".avif",
  ".bmp",
  ".gif"
]);

const MIME_TYPE_BY_EXTENSION: Record<string, string> = {
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".png": "image/png",
  ".webp": "image/webp",
  ".avif": "image/avif",
  ".bmp": "image/bmp",
  ".gif": "image/gif"
};

const PRIORITIZED_BASENAMES = [
  "cover",
  "folder",
  "front",
  "album",
  "artwork"
];

interface CandidateImage {
  name: string;
  score: number;
}

function scoreImageName(fileName: string): number {
  const extension = path.extname(fileName).toLowerCase();
  if (!SUPPORTED_IMAGE_EXTENSIONS.has(extension)) {
    return Number.POSITIVE_INFINITY;
  }

  const baseName = path.basename(fileName, extension).toLowerCase();
  const exactPriority = PRIORITIZED_BASENAMES.indexOf(baseName);
  if (exactPriority >= 0) {
    return exactPriority;
  }

  for (const [index, token] of PRIORITIZED_BASENAMES.entries()) {
    if (baseName.includes(token)) {
      return 100 + index;
    }
  }

  return 1000;
}

export class CoverArtService {
  private readonly directoryCache = new Map<string, string | null>();

  public async resolveForTrack(trackPath: string): Promise<string | null> {
    const directory = path.dirname(trackPath);

    if (this.directoryCache.has(directory)) {
      return this.directoryCache.get(directory) ?? null;
    }

    let entries: Dirent[];
    try {
      entries = await fs.readdir(directory, { withFileTypes: true });
    } catch {
      this.directoryCache.set(directory, null);
      return null;
    }

    const candidates: CandidateImage[] = [];
    for (const entry of entries) {
      if (!entry.isFile()) {
        continue;
      }

      const score = scoreImageName(entry.name);
      if (!Number.isFinite(score)) {
        continue;
      }

      candidates.push({
        name: entry.name,
        score
      });
    }

    candidates.sort((a, b) => {
      if (a.score !== b.score) {
        return a.score - b.score;
      }

      return a.name.localeCompare(b.name);
    });

    const best = candidates[0]?.name;
    const resolved = best ? path.join(directory, best) : null;
    this.directoryCache.set(directory, resolved);
    return resolved;
  }

  public clearCache(): void {
    this.directoryCache.clear();
  }

  public async readAsDataUrl(imagePath: string): Promise<string | null> {
    const extension = path.extname(imagePath).toLowerCase();
    const mimeType = MIME_TYPE_BY_EXTENSION[extension];
    if (!mimeType) {
      return null;
    }

    try {
      const image = await fs.readFile(imagePath);
      return `data:${mimeType};base64,${image.toString("base64")}`;
    } catch {
      return null;
    }
  }
}
