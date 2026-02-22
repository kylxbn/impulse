import { useEffect } from "react";

interface UseGlobalKeyboardShortcutsOptions {
  loaded: boolean;
  contextMenuOpen: boolean;
  settingsOpen: boolean;
  focusedPanel: "browser" | "playlist";
  playlistSelectedTrackId: string | null;
  selectedBrowserPaths: string[];
  closeContextMenu(): void;
  closeSettings(): void;
  playPause(): void;
  movePlaylistSelection(delta: number): void;
  jumpPlaylistSelection(edge: "start" | "end"): void;
  removePlaylistSelection(): void;
  moveBrowserSelection(delta: number): void;
  jumpBrowserSelection(edge: "start" | "end"): void;
  navigateBrowserRight(): void;
  navigateBrowserLeft(): void;
  activatePlaylistSelection(): void;
  activateBrowserSelection(): void;
}

export function useGlobalKeyboardShortcuts({
  loaded,
  contextMenuOpen,
  settingsOpen,
  focusedPanel,
  playlistSelectedTrackId,
  selectedBrowserPaths,
  closeContextMenu,
  closeSettings,
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
}: UseGlobalKeyboardShortcutsOptions): void {
  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent): void => {
      if (!loaded) {
        return;
      }

      if (contextMenuOpen && event.key === "Escape") {
        closeContextMenu();
        return;
      }

      if (contextMenuOpen) {
        return;
      }

      if (settingsOpen && event.key === "Escape") {
        closeSettings();
        return;
      }

      if (
        event.target instanceof HTMLInputElement
        || event.target instanceof HTMLTextAreaElement
        || event.target instanceof HTMLSelectElement
      ) {
        return;
      }

      if (event.key === " ") {
        event.preventDefault();
        playPause();
        return;
      }

      if (focusedPanel === "playlist") {
        if (event.key === "ArrowUp") {
          event.preventDefault();
          movePlaylistSelection(-1);
          return;
        }

        if (event.key === "ArrowDown") {
          event.preventDefault();
          movePlaylistSelection(1);
          return;
        }

        if (event.key === "Home") {
          event.preventDefault();
          jumpPlaylistSelection("start");
          return;
        }

        if (event.key === "End") {
          event.preventDefault();
          jumpPlaylistSelection("end");
          return;
        }

        if (event.key === "Delete" || event.key === "Backspace") {
          event.preventDefault();
          removePlaylistSelection();
          return;
        }
      }

      if (focusedPanel === "browser") {
        if (event.key === "ArrowUp") {
          event.preventDefault();
          moveBrowserSelection(-1);
          return;
        }

        if (event.key === "ArrowDown") {
          event.preventDefault();
          moveBrowserSelection(1);
          return;
        }

        if (event.key === "Home") {
          event.preventDefault();
          jumpBrowserSelection("start");
          return;
        }

        if (event.key === "End") {
          event.preventDefault();
          jumpBrowserSelection("end");
          return;
        }

        if (event.key === "ArrowRight") {
          event.preventDefault();
          navigateBrowserRight();
          return;
        }

        if (event.key === "ArrowLeft") {
          event.preventDefault();
          navigateBrowserLeft();
          return;
        }
      }

      if (event.key === "Enter") {
        if (focusedPanel === "playlist" && playlistSelectedTrackId) {
          activatePlaylistSelection();
          return;
        }

        if (focusedPanel === "browser" && selectedBrowserPaths.length > 0) {
          activateBrowserSelection();
        }
      }
    };

    window.addEventListener("keydown", onKeyDown);
    return () => {
      window.removeEventListener("keydown", onKeyDown);
    };
  }, [
    activateBrowserSelection,
    activatePlaylistSelection,
    closeContextMenu,
    closeSettings,
    contextMenuOpen,
    focusedPanel,
    jumpBrowserSelection,
    jumpPlaylistSelection,
    loaded,
    moveBrowserSelection,
    movePlaylistSelection,
    navigateBrowserLeft,
    navigateBrowserRight,
    playPause,
    playlistSelectedTrackId,
    removePlaylistSelection,
    selectedBrowserPaths,
    settingsOpen
  ]);
}
