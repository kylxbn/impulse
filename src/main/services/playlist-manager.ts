import { randomUUID } from "node:crypto";
import path from "node:path";
import type {
  PlaylistItem,
  PlaylistSnapshot,
  PlaylistSortColumn,
  RepeatMode,
  SortDirection,
  TrackMetadata
} from "../../shared/types.js";

function emptyMetadata(filePath: string): TrackMetadata {
  return {
    trackNumber: null,
    album: "",
    title: path.basename(filePath),
    technical: {
      durationSec: null,
      codec: null,
      bitrateKbps: null,
      sampleRateHz: null,
      bitDepth: null,
      channels: null
    },
    replaygain: {
      trackGainDb: null,
      trackPeakLinear: null,
      trackPeakDb: null
    },
    plr: null
  };
}

function compareNullableNumber(a: number | null, b: number | null): number {
  if (a == null && b == null) {
    return 0;
  }
  if (a == null) {
    return 1;
  }
  if (b == null) {
    return -1;
  }
  return a - b;
}

function clampIndex(index: number, max: number): number {
  return Math.min(Math.max(index, 0), max);
}

export class PlaylistManager {
  private items: PlaylistItem[] = [];
  private selectedTrackId: string | null = null;
  private selectedTrackIds: string[] = [];
  private currentTrackId: string | null = null;
  private sortColumn: PlaylistSortColumn | null = null;
  private sortDirection: SortDirection = "asc";
  private repeatMode: RepeatMode = "off";
  private shuffleEnabled = false;
  private shuffleOrder: string[] = [];

  public getItems(): PlaylistItem[] {
    return this.items;
  }

  public getItemById(trackId: string): PlaylistItem | null {
    return this.items.find((item) => item.id === trackId) ?? null;
  }

  public addPaths(paths: string[], index?: number): PlaylistItem[] {
    const inserted: PlaylistItem[] = [];

    for (const filePath of paths) {
      inserted.push({
        id: randomUUID(),
        path: filePath,
        metadata: emptyMetadata(filePath)
      });
    }

    if (inserted.length === 0) {
      return inserted;
    }

    const insertAt = clampIndex(
      typeof index === "number" ? Math.trunc(index) : this.items.length,
      this.items.length
    );

    this.items.splice(insertAt, 0, ...inserted);
    this.clearSortState();

    if (this.selectedTrackIds.length === 0) {
      const firstInserted = inserted[0];
      this.selectedTrackId = firstInserted?.id ?? null;
      this.selectedTrackIds = firstInserted ? [firstInserted.id] : [];
    }

    this.refreshShuffleOrder();
    return inserted;
  }

  public replaceWithPaths(paths: string[]): PlaylistItem[] {
    this.clearSortState();
    this.items = [];
    this.selectedTrackId = null;
    this.selectedTrackIds = [];
    this.currentTrackId = null;
    this.shuffleOrder = [];
    const inserted = this.addPaths(paths);
    if (inserted.length > 0) {
      const first = inserted[0];
      this.selectedTrackId = first?.id ?? null;
      this.selectedTrackIds = first ? [first.id] : [];
    }
    return inserted;
  }

  public clear(): void {
    this.items = [];
    this.selectedTrackId = null;
    this.selectedTrackIds = [];
    this.currentTrackId = null;
    this.shuffleOrder = [];
    this.clearSortState();
  }

  public removeTrack(trackId: string): { removed: PlaylistItem | null; wasCurrent: boolean } {
    const removed = this.getItemById(trackId);
    const wasCurrent = this.currentTrackId === trackId;
    const result = this.removeTracks([trackId]);

    return {
      removed: result.removedCount > 0 ? removed : null,
      wasCurrent
    };
  }

  public removeTracks(trackIds: string[]): {
    removedCount: number;
    removedCurrent: boolean;
    nextCurrentTrackId: string | null;
  } {
    const toRemove = new Set(trackIds);
    if (toRemove.size === 0 || this.items.length === 0) {
      return {
        removedCount: 0,
        removedCurrent: false,
        nextCurrentTrackId: this.currentTrackId
      };
    }

    const previousItems = this.items;
    const previousCurrentId = this.currentTrackId;
    const previousCurrentIndex = previousCurrentId
      ? previousItems.findIndex((item) => item.id === previousCurrentId)
      : -1;

    const removedIndices: number[] = [];
    const remaining: PlaylistItem[] = [];

    previousItems.forEach((item, index) => {
      if (toRemove.has(item.id)) {
        removedIndices.push(index);
      } else {
        remaining.push(item);
      }
    });

    if (removedIndices.length === 0) {
      return {
        removedCount: 0,
        removedCurrent: false,
        nextCurrentTrackId: this.currentTrackId
      };
    }

    this.items = remaining;
    this.clearSortState();

    const removedCurrent = Boolean(previousCurrentId && toRemove.has(previousCurrentId));
    if (removedCurrent) {
      if (this.items.length === 0) {
        this.currentTrackId = null;
      } else {
        const candidateIndex = clampIndex(previousCurrentIndex, this.items.length - 1);
        this.currentTrackId = this.items[candidateIndex]?.id ?? null;
      }
    } else if (this.currentTrackId && !this.items.some((item) => item.id === this.currentTrackId)) {
      this.currentTrackId = null;
    }

    const activeIds = new Set(this.items.map((item) => item.id));
    const survivingSelected = this.selectedTrackIds.filter((id) => activeIds.has(id));

    if (survivingSelected.length > 0) {
      this.selectedTrackIds = survivingSelected;
      if (this.selectedTrackId && activeIds.has(this.selectedTrackId)) {
        this.selectedTrackId = this.selectedTrackId;
      } else {
        this.selectedTrackId = survivingSelected[0] ?? null;
      }
    } else {
      const firstRemovedIndex = removedIndices[0] ?? 0;
      const fallback =
        this.items[firstRemovedIndex] ??
        this.items[firstRemovedIndex - 1] ??
        this.items[0] ??
        null;

      this.selectedTrackId = fallback?.id ?? null;
      this.selectedTrackIds = fallback ? [fallback.id] : [];
    }

    this.refreshShuffleOrder();

    return {
      removedCount: removedIndices.length,
      removedCurrent,
      nextCurrentTrackId: this.currentTrackId
    };
  }

  public moveTracks(trackIds: string[], index: number): boolean {
    if (this.items.length <= 1 || trackIds.length === 0) {
      return false;
    }

    const requested = new Set(trackIds);
    const moving = this.items.filter((item) => requested.has(item.id));

    if (moving.length === 0 || moving.length === this.items.length) {
      return false;
    }

    const movingIds = new Set(moving.map((item) => item.id));
    const rawTarget = clampIndex(Math.trunc(index), this.items.length);
    const removedBeforeTarget = this.items.reduce((count, item, itemIndex) => {
      if (itemIndex >= rawTarget || !movingIds.has(item.id)) {
        return count;
      }
      return count + 1;
    }, 0);

    const remaining = this.items.filter((item) => !movingIds.has(item.id));
    const targetInRemaining = clampIndex(rawTarget - removedBeforeTarget, remaining.length);

    const reordered = [
      ...remaining.slice(0, targetInRemaining),
      ...moving,
      ...remaining.slice(targetInRemaining)
    ];

    const changed = reordered.some((item, itemIndex) => item.id !== this.items[itemIndex]?.id);
    if (!changed) {
      return false;
    }

    this.items = reordered;
    this.clearSortState();

    const selectedSet = new Set(this.selectedTrackIds);
    this.selectedTrackIds = this.items
      .filter((item) => selectedSet.has(item.id))
      .map((item) => item.id);

    if (this.selectedTrackId && selectedSet.has(this.selectedTrackId)) {
      this.selectedTrackId = this.selectedTrackId;
    } else {
      this.selectedTrackId = this.selectedTrackIds[0] ?? null;
    }

    this.refreshShuffleOrder();
    return true;
  }

  public updateMetadata(trackId: string, metadata: TrackMetadata): void {
    const target = this.items.find((item) => item.id === trackId);
    if (!target) {
      return;
    }

    target.metadata = metadata;
  }

  public setSelectedTrack(trackId: string | null): void {
    if (!trackId) {
      this.selectedTrackId = null;
      this.selectedTrackIds = [];
      return;
    }

    if (this.items.some((item) => item.id === trackId)) {
      this.selectedTrackId = trackId;
      this.selectedTrackIds = [trackId];
    }
  }

  public setSelectedTracks(trackIds: string[], primaryTrackId: string | null): void {
    if (trackIds.length === 0) {
      this.selectedTrackId = null;
      this.selectedTrackIds = [];
      return;
    }

    const requested = new Set(trackIds);
    const normalized = this.items
      .filter((item) => requested.has(item.id))
      .map((item) => item.id);

    if (normalized.length === 0) {
      this.selectedTrackId = null;
      this.selectedTrackIds = [];
      return;
    }

    this.selectedTrackIds = normalized;
    if (primaryTrackId && normalized.includes(primaryTrackId)) {
      this.selectedTrackId = primaryTrackId;
    } else {
      this.selectedTrackId = normalized[0] ?? null;
    }
  }

  public setCurrentTrack(trackId: string | null): void {
    this.currentTrackId = trackId;
    this.refreshShuffleOrder();
  }

  public getSelectedTrackId(): string | null {
    return this.selectedTrackId;
  }

  public getSelectedTrackIds(): string[] {
    return [...this.selectedTrackIds];
  }

  public getCurrentTrackId(): string | null {
    return this.currentTrackId;
  }

  public setRepeatMode(mode: RepeatMode): void {
    this.repeatMode = mode;
  }

  public getRepeatMode(): RepeatMode {
    return this.repeatMode;
  }

  public cycleRepeatMode(): RepeatMode {
    if (this.repeatMode === "off") {
      this.repeatMode = "all";
    } else if (this.repeatMode === "all") {
      this.repeatMode = "one";
    } else {
      this.repeatMode = "off";
    }

    return this.repeatMode;
  }

  public setShuffleEnabled(enabled: boolean): void {
    this.shuffleEnabled = enabled;
    this.refreshShuffleOrder();
  }

  public toggleShuffle(): boolean {
    this.shuffleEnabled = !this.shuffleEnabled;
    this.refreshShuffleOrder();
    return this.shuffleEnabled;
  }

  public isShuffleEnabled(): boolean {
    return this.shuffleEnabled;
  }

  public sortBy(column: PlaylistSortColumn): void {
    if (this.sortColumn === column) {
      this.sortDirection = this.sortDirection === "asc" ? "desc" : "asc";
    } else {
      this.sortColumn = column;
      this.sortDirection = "asc";
    }

    const direction = this.sortDirection === "asc" ? 1 : -1;

    this.items.sort((a, b) => {
      let result = 0;
      switch (this.sortColumn) {
        case "track":
          result = compareNullableNumber(a.metadata.trackNumber, b.metadata.trackNumber);
          break;
        case "album":
          result = a.metadata.album.localeCompare(b.metadata.album);
          break;
        case "title":
          result = a.metadata.title.localeCompare(b.metadata.title);
          break;
        case "length":
          result = compareNullableNumber(a.metadata.technical.durationSec, b.metadata.technical.durationSec);
          break;
        case "codec":
          result = (a.metadata.technical.codec ?? "").localeCompare(b.metadata.technical.codec ?? "");
          break;
        case "bitrate":
          result = compareNullableNumber(a.metadata.technical.bitrateKbps, b.metadata.technical.bitrateKbps);
          break;
        case "replaygain":
          result = compareNullableNumber(a.metadata.replaygain.trackGainDb, b.metadata.replaygain.trackGainDb);
          break;
        case "plr":
          result = compareNullableNumber(a.metadata.plr, b.metadata.plr);
          break;
        default:
          result = 0;
      }

      if (result === 0) {
        result = a.path.localeCompare(b.path);
      }

      return result * direction;
    });

    if (this.selectedTrackIds.length > 0) {
      const selectedSet = new Set(this.selectedTrackIds);
      this.selectedTrackIds = this.items
        .filter((item) => selectedSet.has(item.id))
        .map((item) => item.id);
    }

    this.refreshShuffleOrder();
  }

  public getSortState(): { column: PlaylistSortColumn | null; direction: SortDirection } {
    return {
      column: this.sortColumn,
      direction: this.sortDirection
    };
  }

  public restoreSortState(column: PlaylistSortColumn | null, direction: SortDirection): void {
    this.sortColumn = column;
    this.sortDirection = direction;
  }

  public getNextTrackId(): string | null {
    if (this.items.length === 0) {
      return null;
    }

    if (this.repeatMode === "one" && this.currentTrackId) {
      return this.currentTrackId;
    }

    const order = this.getPlaybackOrder();
    if (order.length === 0) {
      return null;
    }

    if (!this.currentTrackId) {
      return order[0] ?? null;
    }

    const currentIndex = order.indexOf(this.currentTrackId);
    if (currentIndex === -1) {
      return order[0] ?? null;
    }

    const nextIndex = currentIndex + 1;
    if (nextIndex < order.length) {
      return order[nextIndex] ?? null;
    }

    if (this.repeatMode === "all") {
      return order[0] ?? null;
    }

    return null;
  }

  public getPreviousTrackId(): string | null {
    if (this.items.length === 0) {
      return null;
    }

    if (this.repeatMode === "one" && this.currentTrackId) {
      return this.currentTrackId;
    }

    const order = this.getPlaybackOrder();
    if (order.length === 0) {
      return null;
    }

    if (!this.currentTrackId) {
      return order[0] ?? null;
    }

    const currentIndex = order.indexOf(this.currentTrackId);
    if (currentIndex === -1) {
      return order[0] ?? null;
    }

    const previousIndex = currentIndex - 1;
    if (previousIndex >= 0) {
      return order[previousIndex] ?? null;
    }

    if (this.repeatMode === "all") {
      return order[order.length - 1] ?? null;
    }

    return null;
  }

  public getSnapshot(): PlaylistSnapshot {
    return {
      items: this.items,
      selectedTrackId: this.selectedTrackId,
      selectedTrackIds: this.selectedTrackIds,
      currentTrackId: this.currentTrackId,
      sortColumn: this.sortColumn,
      sortDirection: this.sortDirection
    };
  }

  private getPlaybackOrder(): string[] {
    if (this.shuffleEnabled) {
      if (this.shuffleOrder.length !== this.items.length) {
        this.refreshShuffleOrder();
      }
      return [...this.shuffleOrder];
    }

    return this.items.map((item) => item.id);
  }

  private refreshShuffleOrder(): void {
    if (!this.shuffleEnabled) {
      this.shuffleOrder = [];
      return;
    }

    const ids = this.items.map((item) => item.id);
    const currentId = this.currentTrackId;

    if (currentId && ids.includes(currentId)) {
      const others = ids.filter((id) => id !== currentId);
      shuffleInPlace(others);
      this.shuffleOrder = [currentId, ...others];
    } else {
      const copy = [...ids];
      shuffleInPlace(copy);
      this.shuffleOrder = copy;
    }
  }

  private clearSortState(): void {
    this.sortColumn = null;
    this.sortDirection = "asc";
  }
}

function shuffleInPlace<T>(items: T[]): void {
  for (let i = items.length - 1; i > 0; i -= 1) {
    const j = Math.floor(Math.random() * (i + 1));
    [items[i], items[j]] = [items[j] as T, items[i] as T];
  }
}
