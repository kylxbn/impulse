type MetadataTagValue = string | number | Array<string | number>;

function normalizeTagKey(tag: string): string {
  return tag.toLowerCase().replace(/[^a-z0-9]/g, "");
}

function metadataTagToText(value: MetadataTagValue | undefined): string | null {
  if (typeof value === "string") {
    const normalized = value.trim();
    return normalized.length > 0 ? normalized : null;
  }

  if (typeof value === "number") {
    return Number.isFinite(value) ? String(value) : null;
  }

  if (Array.isArray(value)) {
    const flattened = value
      .map((entry) => String(entry).trim())
      .filter((entry) => entry.length > 0)
      .join(", ");

    return flattened.length > 0 ? flattened : null;
  }

  return null;
}

export function pickMetadataTag(
  tags: Record<string, MetadataTagValue> | undefined,
  candidates: string[]
): string | null {
  if (!tags) {
    return null;
  }

  const normalizedLookup = new Map<string, MetadataTagValue>();
  for (const [key, value] of Object.entries(tags)) {
    normalizedLookup.set(normalizeTagKey(key), value);
  }

  for (const candidate of candidates) {
    const value = normalizedLookup.get(normalizeTagKey(candidate));
    const resolved = metadataTagToText(value);
    if (resolved) {
      return resolved;
    }
  }

  return null;
}

export function fileNameFromPath(filePath: string): string {
  const normalized = filePath.replaceAll("\\", "/");
  const fileName = normalized.split("/").at(-1);
  return fileName && fileName.length > 0 ? fileName : filePath;
}
