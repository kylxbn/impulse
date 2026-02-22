import { contextBridge, ipcRenderer, webUtils } from "electron";
import type {
  AppMenuAction,
  AppEvent,
  FilePropertiesSnapshot,
  AppSettings,
  InitialState,
  PlaybackCommand,
  PlaylistCommand
} from "../shared/types.js";

export interface ImpulseAPI {
  init(): Promise<InitialState>;
  playbackCommand(command: PlaybackCommand): Promise<void>;
  playlistCommand(command: PlaylistCommand): Promise<void>;
  getPathForFile(file: File): string | null;
  openSettingsWindow(): Promise<void>;
  openFilePropertiesWindow(path: string): Promise<void>;
  openAlbumArtWindow(path: string): Promise<void>;
  settingsGet(): Promise<AppSettings>;
  settingsSave(settings: AppSettings): Promise<{ ok: true } | { ok: false; error: string }>;
  listBrowserEntries(path: string): Promise<void>;
  readCoverArtAsDataUrl(path: string): Promise<string | null>;
  getFileProperties(path: string): Promise<FilePropertiesSnapshot>;
  subscribe(listener: (event: AppEvent) => void): () => void;
  subscribeMenuActions(listener: (action: AppMenuAction) => void): () => void;
}

const api: ImpulseAPI = {
  init: async () => await ipcRenderer.invoke("app:init"),
  playbackCommand: async (command) => {
    await ipcRenderer.invoke("playback:command", command);
  },
  playlistCommand: async (command) => {
    await ipcRenderer.invoke("playlist:command", command);
  },
  getPathForFile: (file) => {
    try {
      const resolvedPath = webUtils.getPathForFile(file);
      const trimmed = resolvedPath.trim();
      return trimmed.length > 0 ? trimmed : null;
    } catch {
      return null;
    }
  },
  openSettingsWindow: async () => {
    await ipcRenderer.invoke("window:openSettings");
  },
  openFilePropertiesWindow: async (path) => {
    await ipcRenderer.invoke("window:openFileProperties", path);
  },
  openAlbumArtWindow: async (path) => {
    await ipcRenderer.invoke("window:openAlbumArt", path);
  },
  settingsGet: async () => await ipcRenderer.invoke("settings:get"),
  settingsSave: async (settings) => await ipcRenderer.invoke("settings:save", settings),
  listBrowserEntries: async (path) => {
    await ipcRenderer.invoke("browser:listEntries", path);
  },
  readCoverArtAsDataUrl: async (path) => await ipcRenderer.invoke("coverArt:readAsDataUrl", path),
  getFileProperties: async (path) => await ipcRenderer.invoke("file:properties", path),
  subscribe(listener) {
    const callback = (_event: Electron.IpcRendererEvent, payload: AppEvent) => {
      listener(payload);
    };

    ipcRenderer.on("app:event", callback);
    return () => {
      ipcRenderer.off("app:event", callback);
    };
  },
  subscribeMenuActions(listener) {
    const callback = (_event: Electron.IpcRendererEvent, action: AppMenuAction) => {
      listener(action);
    };

    ipcRenderer.on("menu:action", callback);
    return () => {
      ipcRenderer.off("menu:action", callback);
    };
  }
};

contextBridge.exposeInMainWorld("impulse", api);
