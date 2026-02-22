import path from "node:path";
import { promises as fs } from "node:fs";
import type { FileBrowserNode } from "../../shared/types.js";
import { isAudioFile, normalizePath } from "./path-utils.js";

export class FileBrowserService {
  public async listEntries(rootPath: string, requestedPath: string): Promise<FileBrowserNode[]> {
    const normalizedRoot = normalizePath(rootPath);
    const normalizedRequested = normalizePath(requestedPath);

    const relative = path.relative(normalizedRoot, normalizedRequested);
    if (relative.startsWith("..") || path.isAbsolute(relative)) {
      return [];
    }

    let entries;
    try {
      entries = await fs.readdir(normalizedRequested, { withFileTypes: true });
    } catch {
      return [];
    }

    const nodes: FileBrowserNode[] = [];

    for (const entry of entries) {
      const fullPath = path.join(normalizedRequested, entry.name);
      if (entry.isDirectory()) {
        const hasChildren = await this.hasVisibleChildren(fullPath);
        nodes.push({
          path: fullPath,
          name: entry.name,
          type: "directory",
          hasChildren
        });
      } else if (entry.isFile() && isAudioFile(fullPath)) {
        nodes.push({
          path: fullPath,
          name: entry.name,
          type: "file",
          hasChildren: false
        });
      }
    }

    return nodes.sort((a, b) => {
      if (a.type !== b.type) {
        return a.type === "directory" ? -1 : 1;
      }
      return a.name.localeCompare(b.name);
    });
  }

  private async hasVisibleChildren(dirPath: string): Promise<boolean> {
    try {
      const entries = await fs.readdir(dirPath, { withFileTypes: true });
      return entries.some((entry) => {
        if (entry.isDirectory()) {
          return true;
        }
        if (entry.isFile()) {
          return isAudioFile(path.join(dirPath, entry.name));
        }
        return false;
      });
    } catch {
      return false;
    }
  }
}
