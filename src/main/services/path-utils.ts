import path from "node:path";
import { promises as fs } from "node:fs";
import { SUPPORTED_AUDIO_EXTENSIONS } from "../../shared/constants.js";

export function isAudioFile(filePath: string): boolean {
  return SUPPORTED_AUDIO_EXTENSIONS.has(path.extname(filePath).toLowerCase());
}

export async function isDirectory(filePath: string): Promise<boolean> {
  try {
    const stat = await fs.stat(filePath);
    return stat.isDirectory();
  } catch {
    return false;
  }
}

export function normalizePath(filePath: string): string {
  return path.resolve(filePath);
}

export async function listAudioFilesRecursive(rootPath: string): Promise<string[]> {
  const normalized = normalizePath(rootPath);
  const results: string[] = [];
  const stack = [normalized];

  while (stack.length > 0) {
    const current = stack.pop();
    if (!current) {
      continue;
    }

    let entries;
    try {
      entries = await fs.readdir(current, { withFileTypes: true });
    } catch {
      continue;
    }

    entries.sort((a, b) => a.name.localeCompare(b.name));

    for (const entry of entries) {
      const full = path.join(current, entry.name);
      if (entry.isDirectory()) {
        stack.push(full);
      } else if (entry.isFile() && isAudioFile(full)) {
        results.push(full);
      }
    }
  }

  results.sort((a, b) => a.localeCompare(b));
  return results;
}
