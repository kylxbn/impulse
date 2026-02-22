import { memo, useMemo, useState } from "react";
import type { DragEvent, MouseEvent, JSX } from "react";
import { formatDuration, formatSignedDb } from "../../shared/format";
import type {
  PlaybackState,
  PlaylistSnapshot,
  PlaylistSortColumn
} from "../../shared/types";
import { BROWSER_DRAG_MIME } from "./FileBrowser";
import styles from "./PlaylistTable.module.css";

export const PLAYLIST_DRAG_MIME = "application/x-impulse-playlist-track-ids";

interface PlaylistContextPayload {
  x: number;
  y: number;
  trackId: string;
  selectedTrackIds: string[];
}

interface PlaylistSelectionPayload {
  trackIds: string[];
  primaryTrackId: string | null;
  anchorTrackId: string | null;
}

interface PlaylistTableProps {
  playlist: PlaylistSnapshot;
  playbackState: PlaybackState;
  selectionAnchorTrackId: string | null;
  onSelectionChange: (payload: PlaylistSelectionPayload) => void;
  onActivate: (trackId: string) => void;
  onSort: (column: PlaylistSortColumn) => void;
  onContextMenu: (payload: PlaylistContextPayload) => void;
  onInsertPathsAt: (paths: string[], index: number) => void;
  onMoveTracks: (trackIds: string[], index: number) => void;
}

const SORTABLE_COLUMNS: Array<{ key: PlaylistSortColumn; label: string }> = [
  { key: "track", label: "#" },
  { key: "album", label: "Album" },
  { key: "title", label: "Title" },
  { key: "length", label: "Length" },
  { key: "codec", label: "Codec" },
  { key: "bitrate", label: "Bitrate" },
  { key: "replaygain", label: "ReplayGain" },
  { key: "plr", label: "PLR" }
];

const RIGHT_ALIGNED_COLUMNS = new Set<PlaylistSortColumn>([
  "track",
  "length",
  "bitrate",
  "replaygain",
  "plr"
]);

interface SelectionOptions {
  targetId: string;
  shiftKey: boolean;
  toggleKey: boolean;
  preserveIfSelected: boolean;
}

interface SelectionResult {
  trackIds: string[];
  primaryTrackId: string | null;
  anchorTrackId: string | null;
}

function parseDraggedIds(raw: string): string[] {
  if (!raw) {
    return [];
  }

  try {
    const parsed = JSON.parse(raw) as unknown;
    if (!Array.isArray(parsed)) {
      return [];
    }
    return parsed.filter((entry): entry is string => typeof entry === "string");
  } catch {
    return [];
  }
}

function parseExternalDropPaths(dataTransfer: DataTransfer): string[] {
  const paths: string[] = [];
  const seen = new Set<string>();

  const fileList = Array.from(dataTransfer.files);
  for (const file of fileList) {
    const candidate = window.impulse.getPathForFile(file);
    if (!candidate || seen.has(candidate)) {
      continue;
    }
    seen.add(candidate);
    paths.push(candidate);
  }

  const itemList = Array.from(dataTransfer.items);
  for (const item of itemList) {
    if (item.kind !== "file") {
      continue;
    }

    const file = item.getAsFile();
    if (!file) {
      continue;
    }

    const candidate = window.impulse.getPathForFile(file);
    if (!candidate || seen.has(candidate)) {
      continue;
    }
    seen.add(candidate);
    paths.push(candidate);
  }

  return paths;
}

function formatPlr(value: number | null): string {
  if (value == null || !Number.isFinite(value)) {
    return "-";
  }

  return value.toFixed(1);
}

function PlaylistTableComponent({
  playlist,
  playbackState,
  selectionAnchorTrackId,
  onSelectionChange,
  onActivate,
  onSort,
  onContextMenu,
  onInsertPathsAt,
  onMoveTracks
}: PlaylistTableProps): JSX.Element {
  const [dropIndex, setDropIndex] = useState<number | null>(null);

  const orderedTrackIds = useMemo(
    () => playlist.items.map((item) => item.id),
    [playlist.items]
  );

  const selectedTrackIds = useMemo(() => {
    if (playlist.selectedTrackIds.length > 0) {
      return playlist.selectedTrackIds;
    }

    return playlist.selectedTrackId ? [playlist.selectedTrackId] : [];
  }, [playlist.selectedTrackId, playlist.selectedTrackIds]);

  const applySelection = ({
    targetId,
    shiftKey,
    toggleKey,
    preserveIfSelected
  }: SelectionOptions): SelectionResult => {
    const isAlreadySelected = selectedTrackIds.includes(targetId);

    if (preserveIfSelected && isAlreadySelected) {
      return {
        trackIds: selectedTrackIds,
        primaryTrackId: playlist.selectedTrackId,
        anchorTrackId: selectionAnchorTrackId
      };
    }

    if (shiftKey) {
      const anchorId = selectionAnchorTrackId ?? playlist.selectedTrackId;
      if (anchorId) {
        const anchorIndex = orderedTrackIds.indexOf(anchorId);
        const targetIndex = orderedTrackIds.indexOf(targetId);

        if (anchorIndex !== -1 && targetIndex !== -1) {
          const [start, end] = anchorIndex <= targetIndex
            ? [anchorIndex, targetIndex]
            : [targetIndex, anchorIndex];
          const range = orderedTrackIds.slice(start, end + 1);
          const next: SelectionResult = {
            trackIds: range,
            primaryTrackId: targetId,
            anchorTrackId: anchorId
          };
          onSelectionChange(next);
          return next;
        }
      }
    }

    if (toggleKey) {
      const nextSet = new Set(selectedTrackIds);
      if (nextSet.has(targetId)) {
        nextSet.delete(targetId);
      } else {
        nextSet.add(targetId);
      }

      const nextTrackIds = orderedTrackIds.filter((id) => nextSet.has(id));
      const primaryTrackId = nextTrackIds.includes(targetId)
        ? targetId
        : nextTrackIds[0] ?? null;

      const next: SelectionResult = {
        trackIds: nextTrackIds,
        primaryTrackId,
        anchorTrackId: primaryTrackId
      };
      onSelectionChange(next);
      return next;
    }

    const next: SelectionResult = {
      trackIds: [targetId],
      primaryTrackId: targetId,
      anchorTrackId: targetId
    };
    onSelectionChange(next);
    return next;
  };

  const getDropIndexFromEvent = (event: DragEvent<HTMLElement>): number => {
    if (playlist.items.length === 0) {
      return 0;
    }

    const rows = event.currentTarget.querySelectorAll<HTMLTableRowElement>("tr[data-row-index]");
    for (const row of rows) {
      const rowIndexAttr = row.getAttribute("data-row-index");
      const rowIndex = rowIndexAttr ? Number.parseInt(rowIndexAttr, 10) : Number.NaN;
      if (!Number.isFinite(rowIndex)) {
        continue;
      }

      const rect = row.getBoundingClientRect();
      const insertBefore = event.clientY < rect.top + rect.height / 2;
      if (insertBefore) {
        return rowIndex;
      }
    }

    return playlist.items.length;
  };

  const handlePaneDragOver = (event: DragEvent<HTMLDivElement>): void => {
    const types = event.dataTransfer.types;
    const supportsBrowserDrop = types.includes(BROWSER_DRAG_MIME);
    const supportsPlaylistDrop = types.includes(PLAYLIST_DRAG_MIME);
    const supportsExternalDrop = !supportsBrowserDrop && !supportsPlaylistDrop && types.includes("Files");

    if (!supportsBrowserDrop && !supportsPlaylistDrop && !supportsExternalDrop) {
      return;
    }

    event.preventDefault();
    event.dataTransfer.dropEffect = supportsPlaylistDrop ? "move" : "copy";
    setDropIndex(getDropIndexFromEvent(event));
  };

  const handlePaneDrop = (event: DragEvent<HTMLDivElement>): void => {
    const types = event.dataTransfer.types;
    const hasAnyHandledDropType =
      types.includes(PLAYLIST_DRAG_MIME)
      || types.includes(BROWSER_DRAG_MIME)
      || types.includes("Files");
    if (!hasAnyHandledDropType) {
      return;
    }

    event.preventDefault();
    const targetDropIndex = dropIndex ?? getDropIndexFromEvent(event);
    setDropIndex(null);

    const draggedTrackIds = parseDraggedIds(event.dataTransfer.getData(PLAYLIST_DRAG_MIME));
    if (draggedTrackIds.length > 0) {
      onMoveTracks(draggedTrackIds, targetDropIndex);
      return;
    }

    const draggedPaths = parseDraggedIds(event.dataTransfer.getData(BROWSER_DRAG_MIME));
    if (draggedPaths.length > 0) {
      onInsertPathsAt(draggedPaths, targetDropIndex);
      return;
    }

    const externalPaths = parseExternalDropPaths(event.dataTransfer);
    if (externalPaths.length > 0) {
      onInsertPathsAt(externalPaths, targetDropIndex);
    }
  };

  return (
    <section className={`${styles.panel} ${styles.playlistPanel}`}>
      <header className={styles.panelHeader}>
        <h2>Playlist</h2>
        <small>{playlist.items.length} tracks</small>
      </header>
      <div
        className={styles.playlistWrap}
        onDragOver={handlePaneDragOver}
        onDragLeave={(event) => {
          const nextTarget = event.relatedTarget;
          if (!(nextTarget instanceof Node) || !event.currentTarget.contains(nextTarget)) {
            setDropIndex(null);
          }
        }}
        onDrop={handlePaneDrop}
      >
        <table className={styles.playlistTable}>
          <thead>
            <tr>
              <th className={styles.nowHeader} aria-label="Now playing" />
              {SORTABLE_COLUMNS.map((column) => {
                const isActive = playlist.sortColumn === column.key;
                const isRightAligned = RIGHT_ALIGNED_COLUMNS.has(column.key);
                return (
                  <th
                    key={column.key}
                    className={[
                      styles.sortableHeaderCell,
                      isRightAligned ? styles.alignRightHeader : ""
                    ].filter(Boolean).join(" ")}
                  >
                    <button
                      type="button"
                      className={[
                        styles.sortButton,
                        isActive ? styles.sortButtonActive : "",
                        isRightAligned ? styles.sortButtonRight : ""
                      ].filter(Boolean).join(" ")}
                      onClick={() => onSort(column.key)}
                    >
                      {column.label}
                      {isActive ? (playlist.sortDirection === "asc" ? " ▲" : " ▼") : ""}
                    </button>
                  </th>
                );
              })}
            </tr>
          </thead>
          <tbody>
            {playlist.items.map((item, itemIndex) => {
              const isCurrent = playlist.currentTrackId === item.id;
              const isSelected = selectedTrackIds.includes(item.id);

              return (
                <tr
                  key={item.id}
                  data-row-index={itemIndex}
                  className={[
                    styles.bodyRow,
                    isSelected ? styles.rowSelected : "",
                    dropIndex === itemIndex ? styles.rowDropBefore : ""
                  ].filter(Boolean).join(" ")}
                  onClick={(event: MouseEvent<HTMLTableRowElement>) => {
                    applySelection({
                      targetId: item.id,
                      shiftKey: event.shiftKey,
                      toggleKey: event.ctrlKey || event.metaKey,
                      preserveIfSelected: false
                    });
                  }}
                  onContextMenu={(event: MouseEvent<HTMLTableRowElement>) => {
                    event.preventDefault();
                    const nextSelection = applySelection({
                      targetId: item.id,
                      shiftKey: false,
                      toggleKey: false,
                      preserveIfSelected: true
                    });

                    onContextMenu({
                      x: event.clientX,
                      y: event.clientY,
                      trackId: item.id,
                      selectedTrackIds: nextSelection.trackIds
                    });
                  }}
                  onDoubleClick={() => onActivate(item.id)}
                  draggable
                  onDragStart={(event) => {
                    const draggingSelection = selectedTrackIds.includes(item.id)
                      ? selectedTrackIds
                      : [item.id];

                    if (!selectedTrackIds.includes(item.id)) {
                      onSelectionChange({
                        trackIds: [item.id],
                        primaryTrackId: item.id,
                        anchorTrackId: item.id
                      });
                    }

                    event.dataTransfer.setData(PLAYLIST_DRAG_MIME, JSON.stringify(draggingSelection));
                    event.dataTransfer.setData("text/plain", draggingSelection.join("\n"));
                    event.dataTransfer.effectAllowed = "move";
                  }}
                  onDragEnd={() => {
                    setDropIndex(null);
                  }}
                >
                  <td className={styles.nowCell}>
                    {isCurrent ? (
                      playbackState === "playing" ? (
                        <svg className={styles.nowIndicator} viewBox="0 0 16 16" aria-label="Playing" role="img">
                          <polygon points="5,3 13,8 5,13" />
                        </svg>
                      ) : (
                        <svg className={styles.nowIndicator} viewBox="0 0 16 16" aria-label="Paused" role="img">
                          <rect x="4" y="3" width="3" height="10" />
                          <rect x="9" y="3" width="3" height="10" />
                        </svg>
                      )
                    ) : null}
                  </td>
                  <td className={styles.alignRightCell}>
                    {item.metadata.trackNumber != null ? String(item.metadata.trackNumber).slice(-3) : "-"}
                  </td>
                  <td>{item.metadata.album || "-"}</td>
                  <td>{item.metadata.title}</td>
                  <td className={styles.alignRightCell}>{formatDuration(item.metadata.technical.durationSec)}</td>
                  <td>{item.metadata.technical.codec ?? "-"}</td>
                  <td className={styles.alignRightCell}>
                    {item.metadata.technical.bitrateKbps != null ? `${item.metadata.technical.bitrateKbps}` : "-"}
                  </td>
                  <td className={styles.alignRightCell}>{formatSignedDb(item.metadata.replaygain.trackGainDb)}</td>
                  <td className={styles.alignRightCell}>{formatPlr(item.metadata.plr)}</td>
                </tr>
              );
            })}
            {dropIndex === playlist.items.length && playlist.items.length > 0 ? (
              <tr className={styles.dropEndRow}>
                <td colSpan={9} />
              </tr>
            ) : null}
            {dropIndex === 0 && playlist.items.length === 0 ? (
              <tr className={styles.dropEmptyRow}>
                <td colSpan={9} />
              </tr>
            ) : null}
          </tbody>
        </table>
      </div>
    </section>
  );
}

export const PlaylistTable = memo(PlaylistTableComponent);
