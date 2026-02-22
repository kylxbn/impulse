import { memo, type JSX } from "react";
import styles from "./AppContextMenu.module.css";

export type ContextMenuState =
  | { panel: "browser"; x: number; y: number }
  | { panel: "playlist"; x: number; y: number }
  | null;

interface AppContextMenuProps {
  contextMenu: ContextMenuState;
  selectedBrowserPaths: string[];
  primaryBrowserPath: string | null;
  browserPropertiesPath: string | null;
  selectedTrackIds: string[];
  playlistPropertiesPath: string | null;
  onClose(): void;
  onAddBrowserSelection(paths: string[]): void;
  onReplaceFromBrowserSelection(path: string): void;
  onReplaceAndPlayFromBrowserSelection(path: string): void;
  onRemovePlaylistSelection(trackIds: string[]): void;
  onShowFileProperties(path: string): void;
}

function AppContextMenuComponent({
  contextMenu,
  selectedBrowserPaths,
  primaryBrowserPath,
  browserPropertiesPath,
  selectedTrackIds,
  playlistPropertiesPath,
  onClose,
  onAddBrowserSelection,
  onReplaceFromBrowserSelection,
  onReplaceAndPlayFromBrowserSelection,
  onRemovePlaylistSelection,
  onShowFileProperties
}: AppContextMenuProps): JSX.Element | null {
  if (!contextMenu) {
    return null;
  }

  return (
    <div
      className={styles.contextMenu}
      data-context-menu-root
      style={{ left: contextMenu.x, top: contextMenu.y }}
      onClick={(event) => {
        event.stopPropagation();
      }}
      onContextMenu={(event) => {
        event.preventDefault();
      }}
    >
      {contextMenu.panel === "browser" ? (
        <>
          <button
            type="button"
            className={styles.contextMenuItem}
            disabled={selectedBrowserPaths.length === 0}
            onClick={() => {
              onClose();
              if (selectedBrowserPaths.length === 0) {
                return;
              }
              onAddBrowserSelection(selectedBrowserPaths);
            }}
          >
            Add Selected To Playlist
          </button>
          <button
            type="button"
            className={styles.contextMenuItem}
            disabled={!primaryBrowserPath}
            onClick={() => {
              onClose();
              if (!primaryBrowserPath) {
                return;
              }
              onReplaceFromBrowserSelection(primaryBrowserPath);
            }}
          >
            Replace Playlist From Selected
          </button>
          <button
            type="button"
            className={styles.contextMenuItem}
            disabled={!primaryBrowserPath}
            onClick={() => {
              onClose();
              if (!primaryBrowserPath) {
                return;
              }
              onReplaceAndPlayFromBrowserSelection(primaryBrowserPath);
            }}
          >
            Replace And Play From Selected
          </button>
          {browserPropertiesPath ? (
            <button
              type="button"
              className={styles.contextMenuItem}
              onClick={() => {
                onClose();
                onShowFileProperties(browserPropertiesPath);
              }}
            >
              File Properties
            </button>
          ) : null}
        </>
      ) : (
        <>
          <button
            type="button"
            className={styles.contextMenuItem}
            disabled={selectedTrackIds.length === 0}
            onClick={() => {
              onClose();
              onRemovePlaylistSelection(selectedTrackIds);
            }}
          >
            Remove Selected From Playlist
          </button>
          {playlistPropertiesPath ? (
            <button
              type="button"
              className={styles.contextMenuItem}
              onClick={() => {
                onClose();
                onShowFileProperties(playlistPropertiesPath);
              }}
            >
              File Properties
            </button>
          ) : null}
        </>
      )}
    </div>
  );
}

export const AppContextMenu = memo(AppContextMenuComponent);
