import { useMemo, type JSX } from "react";
import type { FilePropertiesSnapshot, PropertyEntry } from "../../shared/types";
import styles from "./FilePropertiesContent.module.css";

interface FilePropertiesContentProps {
  loading: boolean;
  requestedPath: string | null;
  properties: FilePropertiesSnapshot | null;
  error: string | null;
}

function formatBytes(bytes: number): string {
  if (!Number.isFinite(bytes) || bytes < 0) {
    return "unknown";
  }

  if (bytes < 1024) {
    return `${bytes} B`;
  }

  const units = ["KB", "MB", "GB", "TB"];
  let value = bytes;
  let unitIndex = -1;
  while (value >= 1024 && unitIndex < units.length - 1) {
    value /= 1024;
    unitIndex += 1;
  }

  return `${value.toFixed(value >= 100 ? 0 : 2)} ${units[unitIndex]}`;
}

function formatTimestampMs(timestampMs: number): string {
  if (!Number.isFinite(timestampMs) || timestampMs <= 0) {
    return "unknown";
  }

  return `${new Date(timestampMs).toLocaleString()} (${timestampMs})`;
}

function formatNullable(value: number | string | null): string {
  if (typeof value === "number") {
    return Number.isFinite(value) ? String(value) : "unknown";
  }

  if (typeof value === "string") {
    return value.trim() || "unknown";
  }

  return "unknown";
}

function PropertiesTable({ rows, emptyMessage }: { rows: PropertyEntry[]; emptyMessage: string }): JSX.Element {
  if (rows.length === 0) {
    return <div className={styles.propertiesEmpty}>{emptyMessage}</div>;
  }

  return (
    <div className={styles.propertiesTableWrap}>
      <table className={styles.propertiesTable}>
        <thead>
          <tr>
            <th>Key</th>
            <th>Value</th>
          </tr>
        </thead>
        <tbody>
          {rows.map((row, index) => (
            <tr key={`${row.key}:${row.value}:${index}`}>
              <td>{row.key}</td>
              <td>{row.value}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

export function FilePropertiesContent({
  loading,
  requestedPath,
  properties,
  error
}: FilePropertiesContentProps): JSX.Element {
  const summaryRows = useMemo<PropertyEntry[]>(() => {
    if (!properties) {
      return [];
    }

    return [
      { key: "name", value: properties.name },
      { key: "path", value: properties.path },
      { key: "directory", value: properties.directory },
      { key: "extension", value: properties.extension || "(none)" },
      { key: "size_bytes", value: String(properties.stats.sizeBytes) },
      { key: "size_human", value: formatBytes(properties.stats.sizeBytes) }
    ];
  }, [properties]);

  const technicalRows = useMemo<PropertyEntry[]>(() => {
    if (!properties) {
      return [];
    }

    return [
      { key: "track_number", value: formatNullable(properties.metadata.trackNumber) },
      { key: "album", value: formatNullable(properties.metadata.album) },
      { key: "title", value: formatNullable(properties.metadata.title) },
      { key: "duration_sec", value: formatNullable(properties.metadata.technical.durationSec) },
      { key: "codec", value: formatNullable(properties.metadata.technical.codec) },
      { key: "bitrate_kbps", value: formatNullable(properties.metadata.technical.bitrateKbps) },
      { key: "sample_rate_hz", value: formatNullable(properties.metadata.technical.sampleRateHz) },
      { key: "bit_depth", value: formatNullable(properties.metadata.technical.bitDepth) },
      { key: "channels", value: formatNullable(properties.metadata.technical.channels) },
      { key: "replaygain_track_gain_db", value: formatNullable(properties.metadata.replaygain.trackGainDb) },
      { key: "replaygain_track_peak_linear", value: formatNullable(properties.metadata.replaygain.trackPeakLinear) },
      { key: "replaygain_track_peak_db", value: formatNullable(properties.metadata.replaygain.trackPeakDb) },
      { key: "plr_db", value: formatNullable(properties.metadata.plr) }
    ];
  }, [properties]);

  const statRows = useMemo<PropertyEntry[]>(() => {
    if (!properties) {
      return [];
    }

    return [
      { key: "size_bytes", value: String(properties.stats.sizeBytes) },
      { key: "size_human", value: formatBytes(properties.stats.sizeBytes) },
      { key: "dev", value: String(properties.stats.dev) },
      { key: "ino", value: String(properties.stats.ino) },
      { key: "mode_octal", value: properties.stats.mode.toString(8) },
      { key: "nlink", value: String(properties.stats.nlink) },
      { key: "uid", value: String(properties.stats.uid) },
      { key: "gid", value: String(properties.stats.gid) },
      { key: "rdev", value: String(properties.stats.rdev) },
      { key: "blksize", value: String(properties.stats.blksize) },
      { key: "blocks", value: String(properties.stats.blocks) },
      { key: "atime", value: formatTimestampMs(properties.stats.atimeMs) },
      { key: "mtime", value: formatTimestampMs(properties.stats.mtimeMs) },
      { key: "ctime", value: formatTimestampMs(properties.stats.ctimeMs) },
      { key: "birthtime", value: formatTimestampMs(properties.stats.birthtimeMs) }
    ];
  }, [properties]);

  return (
    <>
      {loading ? (
        <p className={styles.propertiesLoading}>Loading properties for {requestedPath ?? "file"}...</p>
      ) : null}

      {!loading && error ? (
        <p className={styles.propertiesError}>{error}</p>
      ) : null}

      {!loading && !error && properties ? (
        <div className={styles.propertiesLayout}>
          <section className={styles.propertiesSection}>
            <h4>Summary</h4>
            <PropertiesTable rows={summaryRows} emptyMessage="No summary data available." />
          </section>

          <section className={styles.propertiesSection}>
            <h4>Filesystem</h4>
            <PropertiesTable rows={statRows} emptyMessage="No filesystem data available." />
          </section>

          <section className={styles.propertiesSection}>
            <h4>Technical Metadata</h4>
            <PropertiesTable rows={technicalRows} emptyMessage="No technical metadata available." />
          </section>

          <section className={styles.propertiesSection}>
            <h4>Raw Tags ({properties.rawTags.length})</h4>
            <PropertiesTable rows={properties.rawTags} emptyMessage="No metadata tags found." />
          </section>

          <section className={styles.propertiesSection}>
            <h4>FFprobe Dump ({properties.ffprobe.length})</h4>
            <PropertiesTable
              rows={properties.ffprobe}
              emptyMessage="FFprobe data unavailable (ffprobe may not be installed)."
            />
          </section>
        </div>
      ) : null}
    </>
  );
}
