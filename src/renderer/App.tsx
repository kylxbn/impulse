import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import type { MouseEvent as ReactMouseEvent, JSX } from "react";
import { clamp } from "../shared/format";
import type {
  AppEvent,
  AppMenuAction,
  AppSettings,
  FileBrowserNode,
  InitialState,
  LyricsSnapshot,
  PlaylistSortColumn,
  PlaybackSnapshot,
  PlaylistSnapshot,
  StatusSnapshot
} from "../shared/types";
import {
  DEFAULT_LEFT_PANE_WIDTH,
  FALLBACK_LYRICS,
  FALLBACK_PLAYBACK,
  FALLBACK_PLAYLIST,
  FALLBACK_SETTINGS,
  FALLBACK_STATUS,
  LEFT_PANE_MIN_PX,
  RIGHT_PANE_MIN_PX
} from "./app-defaults";
import { fileNameFromPath, pickMetadataTag } from "./metadata-utils";
import { AppContextMenu, type ContextMenuState } from "./components/AppContextMenu";
import { FileBrowser, BROWSER_DRAG_MIME } from "./components/FileBrowser";
import { LyricsPanel } from "./components/LyricsPanel";
import { PlaylistTable, PLAYLIST_DRAG_MIME } from "./components/PlaylistTable";
import { TopRegion } from "./components/TopRegion";
import { useGlobalKeyboardShortcuts } from "./hooks/use-global-keyboard-shortcuts";
import styles from "./App.module.css";

interface SplitterDragState {
  startX: number;
  startWidth: number;
}

function parentPathOf(targetPath: string): string | null {
  const normalized = targetPath.replaceAll("\\", "/");
  const separatorIndex = normalized.lastIndexOf("/");
  if (separatorIndex <= 0) {
    return null;
  }

  return normalized.slice(0, separatorIndex);
}

export function App(): JSX.Element {
  const [loaded, setLoaded] = useState(false);
  const [bootError, setBootError] = useState<string | null>(null);
  const [settings, setSettings] = useState<AppSettings>(FALLBACK_SETTINGS);
  const [playback, setPlayback] = useState<PlaybackSnapshot>(FALLBACK_PLAYBACK);
  const [playlist, setPlaylist] = useState<PlaylistSnapshot>(FALLBACK_PLAYLIST);
  const [status, setStatus] = useState<StatusSnapshot>(FALLBACK_STATUS);
  const [lyrics, setLyrics] = useState<LyricsSnapshot>(FALLBACK_LYRICS);
  const [coverArtDataUrl, setCoverArtDataUrl] = useState<string | null>(null);

  const [entriesByPath, setEntriesByPath] = useState<Record<string, FileBrowserNode[]>>({});
  const [expandedPaths, setExpandedPaths] = useState<Set<string>>(new Set());
  const [loadingBrowserPaths, setLoadingBrowserPaths] = useState<Set<string>>(new Set());
  const [selectedBrowserPaths, setSelectedBrowserPaths] = useState<string[]>([]);
  const [browserSelectionAnchorPath, setBrowserSelectionAnchorPath] = useState<string | null>(null);
  const [playlistSelectionAnchorTrackId, setPlaylistSelectionAnchorTrackId] = useState<string | null>(null);
  const [focusedPanel, setFocusedPanel] = useState<"browser" | "playlist">("playlist");
  const [contextMenu, setContextMenu] = useState<ContextMenuState>(null);
  const [leftPaneWidthPx, setLeftPaneWidthPx] = useState(DEFAULT_LEFT_PANE_WIDTH);
  const [splitterDragging, setSplitterDragging] = useState(false);

  const selectedBrowserPathsRef = useRef<string[]>([]);
  const selectedTrackIdsRef = useRef<string[]>([]);
  const middleRegionRef = useRef<HTMLElement | null>(null);
  const splitterDragRef = useRef<SplitterDragState | null>(null);

  const selectedTrackIds = useMemo(() => {
    if (playlist.selectedTrackIds.length > 0) {
      return playlist.selectedTrackIds;
    }
    return playlist.selectedTrackId ? [playlist.selectedTrackId] : [];
  }, [playlist.selectedTrackId, playlist.selectedTrackIds]);

  const playlistOrderedTrackIds = useMemo(
    () => playlist.items.map((item) => item.id),
    [playlist.items]
  );

  const browserTreeState = useMemo(() => {
    const nodeByPath = new Map<string, FileBrowserNode>();
    const visiblePaths: string[] = [];
    const rootEntries = entriesByPath[settings.musicRoot] ?? [];

    const walk = (node: FileBrowserNode): void => {
      nodeByPath.set(node.path, node);
      visiblePaths.push(node.path);

      if (node.type !== "directory" || !expandedPaths.has(node.path)) {
        return;
      }

      const children = entriesByPath[node.path] ?? [];
      for (const child of children) {
        walk(child);
      }
    };

    for (const entry of rootEntries) {
      walk(entry);
    }

    return {
      visiblePaths,
      visiblePathSet: new Set(visiblePaths),
      nodeByPath
    };
  }, [entriesByPath, expandedPaths, settings.musicRoot]);

  const primaryBrowserPath = selectedBrowserPaths[0] ?? null;

  const browserPropertiesPath = useMemo(() => {
    const activeSelection = contextMenu?.panel === "browser"
      ? selectedBrowserPathsRef.current
      : selectedBrowserPaths;

    if (activeSelection.length !== 1) {
      return null;
    }

    const candidate = activeSelection[0];
    if (!candidate) {
      return null;
    }

    const node = browserTreeState.nodeByPath.get(candidate);
    return node?.type === "file" ? candidate : null;
  }, [browserTreeState.nodeByPath, contextMenu, selectedBrowserPaths]);

  const playlistPropertiesPath = useMemo(() => {
    const activeSelection = contextMenu?.panel === "playlist"
      ? selectedTrackIdsRef.current
      : selectedTrackIds;

    if (activeSelection.length !== 1) {
      return null;
    }

    const selectedTrackId = activeSelection[0];
    if (!selectedTrackId) {
      return null;
    }

    const selectedItem = playlist.items.find((item) => item.id === selectedTrackId);
    return selectedItem?.path ?? null;
  }, [contextMenu, playlist.items, selectedTrackIds]);

  const updateBrowserSelection = useCallback((paths: string[], anchorPath: string | null): void => {
    setSelectedBrowserPaths(paths);
    setBrowserSelectionAnchorPath(anchorPath);
    selectedBrowserPathsRef.current = paths;
  }, []);

  const removeSelectedTracks = useCallback((trackIds: string[]): void => {
    if (trackIds.length === 0) {
      return;
    }

    void window.impulse.playlistCommand({ type: "removeTracks", trackIds });
  }, []);

  useEffect(() => {
    selectedBrowserPathsRef.current = selectedBrowserPaths;
  }, [selectedBrowserPaths]);

  useEffect(() => {
    selectedTrackIdsRef.current = selectedTrackIds;
  }, [selectedTrackIds]);

  useEffect(() => {
    setPlaylistSelectionAnchorTrackId((previous) => {
      if (previous && playlist.items.some((item) => item.id === previous)) {
        return previous;
      }
      return playlist.selectedTrackId;
    });
  }, [playlist.items, playlist.selectedTrackId]);

  useEffect(() => {
    if (!contextMenu) {
      return;
    }

    const closeMenuFromMouse = (event: MouseEvent): void => {
      const target = event.target;
      if (target instanceof Element && target.closest("[data-context-menu-root]")) {
        return;
      }
      setContextMenu(null);
    };

    const closeMenu = (): void => {
      setContextMenu(null);
    };

    const closeOnEscape = (event: KeyboardEvent): void => {
      if (event.key === "Escape") {
        setContextMenu(null);
      }
    };

    window.addEventListener("mousedown", closeMenuFromMouse);
    window.addEventListener("resize", closeMenu);
    window.addEventListener("blur", closeMenu);
    window.addEventListener("keydown", closeOnEscape);

    return () => {
      window.removeEventListener("mousedown", closeMenuFromMouse);
      window.removeEventListener("resize", closeMenu);
      window.removeEventListener("blur", closeMenu);
      window.removeEventListener("keydown", closeOnEscape);
    };
  }, [contextMenu]);

  useEffect(() => {
    const clampLeftPane = (): void => {
      const containerWidth = middleRegionRef.current?.clientWidth ?? window.innerWidth;
      const maxWidth = Math.max(LEFT_PANE_MIN_PX, containerWidth - RIGHT_PANE_MIN_PX);
      setLeftPaneWidthPx((previous) => clamp(previous, LEFT_PANE_MIN_PX, maxWidth));
    };

    clampLeftPane();
    window.addEventListener("resize", clampLeftPane);
    return () => {
      window.removeEventListener("resize", clampLeftPane);
    };
  }, []);

  useEffect(() => {
    const preventExternalDropNavigation = (event: DragEvent): void => {
      const dataTransfer = event.dataTransfer;
      if (!dataTransfer) {
        return;
      }

      const types = dataTransfer.types;
      const hasInternalDrag = types.includes(BROWSER_DRAG_MIME) || types.includes(PLAYLIST_DRAG_MIME);

      // Block Chromium's default "open dropped content" behavior for any non-Impulse drag payload.
      if (!hasInternalDrag) {
        event.preventDefault();
      }
    };

    window.addEventListener("dragover", preventExternalDropNavigation);
    window.addEventListener("drop", preventExternalDropNavigation);

    return () => {
      window.removeEventListener("dragover", preventExternalDropNavigation);
      window.removeEventListener("drop", preventExternalDropNavigation);
    };
  }, []);

  useEffect(() => {
    const handleMouseMove = (event: MouseEvent): void => {
      const dragState = splitterDragRef.current;
      if (!dragState) {
        return;
      }

      const containerWidth = middleRegionRef.current?.clientWidth ?? window.innerWidth;
      const maxWidth = Math.max(LEFT_PANE_MIN_PX, containerWidth - RIGHT_PANE_MIN_PX);
      const nextWidth = dragState.startWidth + (event.clientX - dragState.startX);
      setLeftPaneWidthPx(clamp(nextWidth, LEFT_PANE_MIN_PX, maxWidth));
    };

    const handleMouseUp = (): void => {
      if (!splitterDragRef.current) {
        return;
      }

      splitterDragRef.current = null;
      setSplitterDragging(false);
    };

    window.addEventListener("mousemove", handleMouseMove);
    window.addEventListener("mouseup", handleMouseUp);

    return () => {
      window.removeEventListener("mousemove", handleMouseMove);
      window.removeEventListener("mouseup", handleMouseUp);
    };
  }, []);

  const sortPlaylist = useCallback((column: PlaylistSortColumn): void => {
    void window.impulse.playlistCommand({ type: "sort", column });
  }, []);

  const applyMenuAction = useCallback((action: AppMenuAction): void => {
    switch (action) {
      case "add-selected-path":
        if (selectedBrowserPathsRef.current.length > 0) {
          void window.impulse.playlistCommand({
            type: "addPaths",
            paths: selectedBrowserPathsRef.current
          });
        }
        return;
      case "replace-selected-path": {
        const selectedPath = selectedBrowserPathsRef.current[0];
        if (selectedPath) {
          void window.impulse.playlistCommand({ type: "replaceFromPath", path: selectedPath });
        }
        return;
      }
      case "open-settings":
        void window.impulse.openSettingsWindow();
        return;
      case "remove-selected-track":
        removeSelectedTracks(selectedTrackIdsRef.current);
        return;
      case "clear-playlist":
        void window.impulse.playlistCommand({ type: "clear" });
        return;
      case "focus-browser":
        setFocusedPanel("browser");
        return;
      case "focus-playlist":
        setFocusedPanel("playlist");
        return;
      case "sort-track":
        sortPlaylist("track");
        return;
      case "sort-album":
        sortPlaylist("album");
        return;
      case "sort-title":
        sortPlaylist("title");
        return;
      case "sort-length":
        sortPlaylist("length");
        return;
      case "sort-codec":
        sortPlaylist("codec");
        return;
      case "sort-bitrate":
        sortPlaylist("bitrate");
        return;
      case "sort-replaygain":
        sortPlaylist("replaygain");
        return;
      case "sort-plr":
        sortPlaylist("plr");
        return;
      default:
        return;
    }
  }, [removeSelectedTracks, sortPlaylist]);

  useEffect(() => {
    let unlistenEvents: (() => void) | null = null;
    let unlistenMenuActions: (() => void) | null = null;

    const bootstrap = async (): Promise<void> => {
      try {
        const bridge = (window as Window & { impulse?: typeof window.impulse }).impulse;
        if (!bridge || typeof bridge.init !== "function") {
          throw new Error("Preload bridge not available. This usually means preload failed to load.");
        }

        const initial = (await bridge.init()) as InitialState;
        setSettings(initial.settings);
        setPlayback(initial.playback);
        setPlaylist(initial.playlist);
        setStatus(initial.status);
        setLyrics(initial.lyrics);
        setLoaded(true);
        setBootError(null);

        updateBrowserSelection([], null);
        setExpandedPaths(new Set([initial.settings.musicRoot]));
        await requestDirectory(initial.settings.musicRoot);

        unlistenEvents = bridge.subscribe((event: AppEvent) => {
          switch (event.type) {
            case "playback.snapshot":
              setPlayback(event.payload);
              return;
            case "playlist.snapshot":
              setPlaylist(event.payload);
              return;
            case "playlist.rowMetaUpdated":
              setPlaylist((previous) => {
                const rowIndex = previous.items.findIndex((item) => item.id === event.payload.trackId);
                if (rowIndex === -1) {
                  return previous;
                }

                const target = previous.items[rowIndex];
                if (!target) {
                  return previous;
                }

                const nextItems = [...previous.items];
                nextItems[rowIndex] = {
                  ...target,
                  metadata: event.payload.metadata
                };

                return {
                  ...previous,
                  items: nextItems
                };
              });
              return;
            case "browser.entries":
              setEntriesByPath((previous) => ({
                ...previous,
                [event.payload.path]: event.payload.entries
              }));
              return;
            case "settings.updated":
              setSettings(event.payload);
              setEntriesByPath({});
              setExpandedPaths(new Set([event.payload.musicRoot]));
              setLoadingBrowserPaths(new Set());
              updateBrowserSelection([], null);
              void requestDirectory(event.payload.musicRoot);
              return;
            case "lyrics.snapshot":
              setLyrics(event.payload);
              return;
            case "status.snapshot":
              setStatus(event.payload);
              return;
            case "status.message":
              return;
            case "backend.error":
              return;
            default:
              return;
          }
        });

        unlistenMenuActions = bridge.subscribeMenuActions((action) => {
          applyMenuAction(action);
        });
      } catch (error) {
        const message = (error as Error).message || String(error);
        setBootError(`Startup failed: ${message}`);
      }
    };

    void bootstrap();

    return () => {
      if (unlistenEvents) {
        unlistenEvents();
      }
      if (unlistenMenuActions) {
        unlistenMenuActions();
      }
    };
  }, [applyMenuAction, updateBrowserSelection]);

  const trackReadout = useMemo(() => {
    const currentIndex = playback.currentTrackId
      ? playlist.items.findIndex((item) => item.id === playback.currentTrackId)
      : -1;

    if (currentIndex === -1) {
      return `Track -/${playlist.items.length}`;
    }

    return `Track ${currentIndex + 1}/${playlist.items.length}`;
  }, [playback.currentTrackId, playlist.items]);

  const nowPlaying = useMemo(() => {
    const currentTrack = playback.currentTrackId
      ? playlist.items.find((item) => item.id === playback.currentTrackId) ?? null
      : null;

    if (!currentTrack) {
      return {
        artistAndAlbum: "No track playing",
        title: "Idle"
      };
    }

    const artist =
      pickMetadataTag(currentTrack.metadata.tags, ["artist", "album artist", "albumartist", "performer", "composer"]) ??
      "Unknown Artist";
    const album = currentTrack.metadata.album.trim() || "Unknown Album";
    const title = currentTrack.metadata.title.trim() || fileNameFromPath(currentTrack.path);

    return {
      artistAndAlbum: `${artist} - ${album}`,
      title
    };
  }, [playback.currentTrackId, playlist.items]);

  const statusLine = useMemo(() => {
    if (status.backendError) {
      return status.backendError;
    }

    if (status.temporaryMessage) {
      return status.temporaryMessage;
    }

    if (status.trackInfo) {
      return `Codec: ${status.trackInfo.codec} | Container: ${status.trackInfo.container} | SR: ${status.trackInfo.sampleRate} | BD: ${status.trackInfo.bitDepth} | Out: ${status.trackInfo.outputFormat} | CH: ${status.trackInfo.channels} | BR: ${status.trackInfo.bitrate}`;
    }

    return "Codec: - | Container: - | SR: - | BD: - | Out: - | CH: - | BR: -";
  }, [status]);
  const coverArtPath = status.trackInfo?.coverArtPath ?? null;

  useEffect(() => {
    let cancelled = false;

    if (!coverArtPath) {
      setCoverArtDataUrl(null);
      return () => {
        cancelled = true;
      };
    }

    void window.impulse.readCoverArtAsDataUrl(coverArtPath).then((dataUrl) => {
      if (cancelled) {
        return;
      }

      setCoverArtDataUrl(dataUrl);
    }).catch(() => {
      if (!cancelled) {
        setCoverArtDataUrl(null);
      }
    });

    return () => {
      cancelled = true;
    };
  }, [coverArtPath]);

  const requestDirectory = useCallback(async (targetPath: string): Promise<void> => {
    setLoadingBrowserPaths((previous) => {
      if (previous.has(targetPath)) {
        return previous;
      }

      const next = new Set(previous);
      next.add(targetPath);
      return next;
    });

    try {
      await window.impulse.listBrowserEntries(targetPath);
    } finally {
      setLoadingBrowserPaths((previous) => {
        if (!previous.has(targetPath)) {
          return previous;
        }

        const next = new Set(previous);
        next.delete(targetPath);
        return next;
      });
    }
  }, []);

  useEffect(() => {
    if (!loaded) {
      return;
    }

    const hasLoadedRootEntries = Object.prototype.hasOwnProperty.call(entriesByPath, settings.musicRoot);
    const isRootLoadInFlight = loadingBrowserPaths.has(settings.musicRoot);

    if (!hasLoadedRootEntries && !isRootLoadInFlight) {
      void requestDirectory(settings.musicRoot);
    }
  }, [entriesByPath, loaded, loadingBrowserPaths, requestDirectory, settings.musicRoot]);

  const toggleDirectory = (targetPath: string): void => {
    setExpandedPaths((previous) => {
      const next = new Set(previous);
      if (next.has(targetPath)) {
        next.delete(targetPath);
      } else {
        next.add(targetPath);
        if (!entriesByPath[targetPath]) {
          void requestDirectory(targetPath);
        }
      }
      return next;
    });
  };

  const selectPlaylistTrack = useCallback((trackId: string): void => {
    if (!trackId) {
      return;
    }

    selectedTrackIdsRef.current = [trackId];
    setPlaylistSelectionAnchorTrackId(trackId);
    void window.impulse.playlistCommand({
      type: "select",
      trackIds: [trackId],
      primaryTrackId: trackId
    });
  }, []);

  const movePlaylistSelection = useCallback((delta: number): void => {
    if (playlistOrderedTrackIds.length === 0) {
      return;
    }

    const baseId = playlist.selectedTrackId ?? selectedTrackIds[0] ?? null;
    const currentIndex = baseId ? playlistOrderedTrackIds.indexOf(baseId) : -1;
    const fallbackIndex = delta > 0 ? -1 : playlistOrderedTrackIds.length;
    const nextIndex = clamp(
      (currentIndex === -1 ? fallbackIndex : currentIndex) + delta,
      0,
      playlistOrderedTrackIds.length - 1
    );

    const nextTrackId = playlistOrderedTrackIds[nextIndex];
    if (nextTrackId) {
      selectPlaylistTrack(nextTrackId);
    }
  }, [playlist.selectedTrackId, playlistOrderedTrackIds, selectPlaylistTrack, selectedTrackIds]);

  const jumpPlaylistSelection = useCallback((edge: "start" | "end"): void => {
    if (playlistOrderedTrackIds.length === 0) {
      return;
    }

    const nextTrackId = edge === "start"
      ? playlistOrderedTrackIds[0]
      : playlistOrderedTrackIds[playlistOrderedTrackIds.length - 1];

    if (nextTrackId) {
      selectPlaylistTrack(nextTrackId);
    }
  }, [playlistOrderedTrackIds, selectPlaylistTrack]);

  const selectBrowserPath = useCallback((targetPath: string): void => {
    if (!targetPath) {
      return;
    }

    updateBrowserSelection([targetPath], targetPath);
  }, [updateBrowserSelection]);

  const moveBrowserSelection = useCallback((delta: number): void => {
    const ordered = browserTreeState.visiblePaths;
    if (ordered.length === 0) {
      return;
    }

    const basePath = selectedBrowserPaths[0] ?? null;
    const currentIndex = basePath ? ordered.indexOf(basePath) : -1;
    const fallbackIndex = delta > 0 ? -1 : ordered.length;
    const nextIndex = clamp(
      (currentIndex === -1 ? fallbackIndex : currentIndex) + delta,
      0,
      ordered.length - 1
    );

    const nextPath = ordered[nextIndex];
    if (nextPath) {
      selectBrowserPath(nextPath);
    }
  }, [browserTreeState.visiblePaths, selectBrowserPath, selectedBrowserPaths]);

  const jumpBrowserSelection = useCallback((edge: "start" | "end"): void => {
    const ordered = browserTreeState.visiblePaths;
    if (ordered.length === 0) {
      return;
    }

    const nextPath = edge === "start"
      ? ordered[0]
      : ordered[ordered.length - 1];

    if (nextPath) {
      selectBrowserPath(nextPath);
    }
  }, [browserTreeState.visiblePaths, selectBrowserPath]);

  const navigateBrowserRight = useCallback((): void => {
    const selectedPath = selectedBrowserPaths[0];
    if (!selectedPath) {
      return;
    }

    const node = browserTreeState.nodeByPath.get(selectedPath);
    if (!node || node.type !== "directory") {
      return;
    }

    if (!expandedPaths.has(selectedPath)) {
      toggleDirectory(selectedPath);
      return;
    }

    const children = entriesByPath[selectedPath] ?? [];
    if (children.length > 0) {
      const nextPath = children[0]?.path;
      if (nextPath) {
        selectBrowserPath(nextPath);
      }
      return;
    }

    if (node.hasChildren) {
      void requestDirectory(selectedPath);
    }
  }, [browserTreeState.nodeByPath, entriesByPath, expandedPaths, requestDirectory, selectBrowserPath, selectedBrowserPaths]);

  const navigateBrowserLeft = useCallback((): void => {
    const selectedPath = selectedBrowserPaths[0];
    if (!selectedPath) {
      return;
    }

    const node = browserTreeState.nodeByPath.get(selectedPath);
    if (node?.type === "directory" && expandedPaths.has(selectedPath)) {
      toggleDirectory(selectedPath);
      return;
    }

    let parent = parentPathOf(selectedPath);
    while (parent) {
      if (browserTreeState.visiblePathSet.has(parent)) {
        selectBrowserPath(parent);
        return;
      }
      parent = parentPathOf(parent);
    }
  }, [browserTreeState.nodeByPath, browserTreeState.visiblePathSet, expandedPaths, selectBrowserPath, selectedBrowserPaths]);

  const playPause = useCallback((): void => {
    void window.impulse.playbackCommand({ type: "playPause" });
  }, []);

  const activatePlaylistSelection = useCallback((): void => {
    const selected = playlist.selectedTrackId;
    if (!selected) {
      return;
    }

    void window.impulse.playbackCommand({ type: "playTrack", trackId: selected });
  }, [playlist.selectedTrackId]);

  const activateBrowserSelection = useCallback((): void => {
    if (selectedBrowserPaths.length === 0) {
      return;
    }

    void window.impulse.playlistCommand({ type: "addPaths", paths: selectedBrowserPaths });
  }, [selectedBrowserPaths]);

  const removePlaylistSelection = useCallback((): void => {
    removeSelectedTracks(selectedTrackIdsRef.current);
  }, [removeSelectedTracks]);

  const closeContextMenu = useCallback((): void => {
    setContextMenu(null);
  }, []);

  const closeSettingsShortcut = useCallback((): void => {
    // Settings now opens as a standalone child window.
  }, []);


  useGlobalKeyboardShortcuts({
    loaded,
    contextMenuOpen: Boolean(contextMenu),
    settingsOpen: false,
    focusedPanel,
    playlistSelectedTrackId: playlist.selectedTrackId,
    selectedBrowserPaths,
    closeContextMenu,
    closeSettings: closeSettingsShortcut,
    playPause,
    movePlaylistSelection,
    jumpPlaylistSelection,
    removePlaylistSelection,
    moveBrowserSelection,
    jumpBrowserSelection,
    navigateBrowserRight,
    navigateBrowserLeft,
    activatePlaylistSelection,
    activateBrowserSelection
  });

  if (!loaded) {
    return (
      <div className={styles.loadingShell}>
        {bootError ? (
          <div className={styles.bootError}>
            <strong>Impulse failed to start</strong>
            <div>{bootError}</div>
            {import.meta.env.DEV ? (
              <div className={styles.bootHint}>
                Press <code>F12</code> or <code>Ctrl+Shift+I</code> for DevTools.
              </div>
            ) : null}
          </div>
        ) : (
          "Loading Impulse..."
        )}
      </div>
    );
  }

  return (
    <div className={styles.appShell}>
      <TopRegion
        coverArtDataUrl={coverArtDataUrl}
        nowPlaying={nowPlaying}
        playback={playback}
        trackReadout={trackReadout}
        onSetRepeatMode={(mode) => {
          void window.impulse.playbackCommand({ type: "setRepeatMode", mode });
        }}
        onSetShuffle={(enabled) => {
          void window.impulse.playbackCommand({ type: "setShuffle", enabled });
        }}
        onPrevious={() => {
          void window.impulse.playbackCommand({ type: "previous" });
        }}
        onStop={() => {
          void window.impulse.playbackCommand({ type: "stop" });
        }}
        onPlayPause={() => {
          void window.impulse.playbackCommand({ type: "playPause" });
        }}
        onNext={() => {
          void window.impulse.playbackCommand({ type: "next" });
        }}
        onSeekAbsolute={(seconds) => {
          void window.impulse.playbackCommand({ type: "seekAbsolute", seconds });
        }}
        onSetVolume={(percent) => {
          void window.impulse.playbackCommand({ type: "setVolume", percent });
        }}
        onOpenAlbumArt={() => {
          if (!coverArtPath) {
            return;
          }

          void window.impulse.openAlbumArtWindow(coverArtPath);
        }}
      />

      <main
        ref={middleRegionRef}
        className={`${styles.middleRegion} ${splitterDragging ? styles.isResizing : ""}`.trim()}
      >
        <div
          className={`${styles.leftPane} ${focusedPanel === "browser" ? styles.focusedPane : ""}`.trim()}
          style={{ width: `${leftPaneWidthPx}px` }}
          onClick={() => setFocusedPanel("browser")}
        >
          <FileBrowser
            rootPath={settings.musicRoot}
            entriesByPath={entriesByPath}
            expandedPaths={expandedPaths}
            loadingPaths={loadingBrowserPaths}
            selectedPaths={selectedBrowserPaths}
            selectionAnchorPath={browserSelectionAnchorPath}
            onSelectionChange={(paths, anchorPath) => {
              updateBrowserSelection(paths, anchorPath);
            }}
            onToggle={toggleDirectory}
            onActivate={(targetPath) => {
              void window.impulse.playlistCommand({ type: "addPaths", paths: [targetPath] });
            }}
            onContextMenu={(payload) => {
              updateBrowserSelection(payload.selectedPaths, payload.targetPath);
              selectedBrowserPathsRef.current = payload.selectedPaths;
              setFocusedPanel("browser");
              setContextMenu({ panel: "browser", x: payload.x, y: payload.y });
            }}
          />
        </div>

        <div
          className={styles.paneResizer}
          onMouseDown={(event: ReactMouseEvent<HTMLDivElement>) => {
            if (event.button !== 0) {
              return;
            }

            event.preventDefault();
            splitterDragRef.current = {
              startX: event.clientX,
              startWidth: leftPaneWidthPx
            };
            setSplitterDragging(true);
          }}
        />

        <div
          className={`${styles.rightPane} ${focusedPanel === "playlist" ? styles.focusedPane : ""}`.trim()}
          onClick={() => setFocusedPanel("playlist")}
        >
          <PlaylistTable
            playlist={playlist}
            playbackState={playback.state}
            selectionAnchorTrackId={playlistSelectionAnchorTrackId}
            onSelectionChange={({ trackIds, primaryTrackId, anchorTrackId }) => {
              setPlaylistSelectionAnchorTrackId(anchorTrackId);
              void window.impulse.playlistCommand({
                type: "select",
                trackIds,
                primaryTrackId
              });
            }}
            onActivate={(trackId) => {
              void window.impulse.playbackCommand({ type: "playTrack", trackId });
            }}
            onSort={(column) => {
              sortPlaylist(column);
            }}
            onContextMenu={(payload) => {
              setFocusedPanel("playlist");
              setPlaylistSelectionAnchorTrackId(payload.trackId);
              selectedTrackIdsRef.current = payload.selectedTrackIds;
              void window.impulse.playlistCommand({
                type: "select",
                trackIds: payload.selectedTrackIds,
                primaryTrackId: payload.trackId
              });
              setContextMenu({ panel: "playlist", x: payload.x, y: payload.y });
            }}
            onInsertPathsAt={(paths, index) => {
              void window.impulse.playlistCommand({ type: "addPaths", paths, index });
            }}
            onMoveTracks={(trackIds, index) => {
              void window.impulse.playlistCommand({ type: "moveTracks", trackIds, index });
            }}
          />
        </div>
      </main>

      <LyricsPanel lyrics={lyrics} />

      <footer className={`${styles.statusBar} ${status.backendError ? styles.statusBarError : ""}`.trim()}>
        {statusLine}
      </footer>

      <AppContextMenu
        contextMenu={contextMenu}
        selectedBrowserPaths={selectedBrowserPaths}
        primaryBrowserPath={primaryBrowserPath}
        browserPropertiesPath={browserPropertiesPath}
        selectedTrackIds={selectedTrackIds}
        playlistPropertiesPath={playlistPropertiesPath}
        onClose={() => {
          setContextMenu(null);
        }}
        onAddBrowserSelection={(paths) => {
          void window.impulse.playlistCommand({ type: "addPaths", paths });
        }}
        onReplaceFromBrowserSelection={(path) => {
          void window.impulse.playlistCommand({ type: "replaceFromPath", path });
        }}
        onReplaceAndPlayFromBrowserSelection={(path) => {
          void window.impulse.playlistCommand({ type: "replaceFromPathAndPlay", path });
        }}
        onRemovePlaylistSelection={(trackIds) => {
          removeSelectedTracks(trackIds);
        }}
        onShowFileProperties={(path) => {
          void window.impulse.openFilePropertiesWindow(path);
        }}
      />
    </div>
  );
}
