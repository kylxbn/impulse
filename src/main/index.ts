import path from "node:path";
import { promises as fs } from "node:fs";
import { fileURLToPath } from "node:url";
import {
  app,
  BrowserWindow,
  dialog,
  ipcMain,
  Menu,
  type IpcMainInvokeEvent,
  type MenuItemConstructorOptions
} from "electron";
import type {
  AppMenuAction,
  AppSettings,
  InitialState,
  PlaybackCommand,
  PlaylistCommand
} from "../shared/types.js";
import { SUPPORTED_AUDIO_EXTENSIONS } from "../shared/constants.js";
import { AppController } from "./services/app-controller.js";
import { MprisBridge } from "./services/mpris-bridge.js";
import { isAudioFile } from "./services/path-utils.js";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const isDevelopmentMode = !app.isPackaged && Boolean(process.env.ELECTRON_RENDERER_URL);
const appIconPath = path.join(__dirname, "..", "..", "..", "impulse-512.png");

let mainWindow: BrowserWindow | null = null;
let controller: AppController | null = null;
let initialState: InitialState | null = null;
let controllerInitPromise: Promise<void> | null = null;
let controllerShutdownPromise: Promise<void> | null = null;
let mprisBridge: MprisBridge | null = null;
let settingsWindow: BrowserWindow | null = null;
let filePropertiesWindow: BrowserWindow | null = null;
let albumArtWindow: BrowserWindow | null = null;
let quitAfterShutdown = false;
const hasSingleInstanceLock = app.requestSingleInstanceLock();
const pendingExternalOpenTargetBatches: string[][] = [];

if (process.platform === "linux") {
  // I mean... nobody's running this except me, and I use Linux, so...
  // Wayland FTW!!!
  app.commandLine.appendSwitch("ozone-platform-hint", "auto");
}

function configureUserDataPath(): void {
  const appDataPath = app.getPath("appData");
  const target = path.join(appDataPath, "impulse");

  if (app.getPath("userData") !== target) {
    app.setPath("userData", target);
  }
}

configureUserDataPath();

function broadcastEvent(channel: string, payload: unknown): void {
  if (mainWindow && !mainWindow.isDestroyed()) {
    mainWindow.webContents.send(channel, payload);
  }
}

function focusMainWindow(): void {
  if (!mainWindow || mainWindow.isDestroyed()) {
    createMainWindow();
    return;
  }

  if (mainWindow.isMinimized()) {
    mainWindow.restore();
  }

  mainWindow.show();
  mainWindow.focus();
}

function dispatchMenuAction(action: AppMenuAction): void {
  broadcastEvent("menu:action", action);
}

function listTrustedWindows(): BrowserWindow[] {
  return [
    mainWindow,
    settingsWindow,
    filePropertiesWindow,
    albumArtWindow
  ].filter((entry): entry is BrowserWindow => Boolean(entry && !entry.isDestroyed()));
}

function assertTrustedIpcSender(event: IpcMainInvokeEvent): void {
  const senderId = event.sender.id;
  const trusted = listTrustedWindows().some((entry) => entry.webContents.id === senderId);
  if (!trusted) {
    throw new Error("Unauthorized IPC sender.");
  }
}

function applyWindowNavigationGuards(target: BrowserWindow): void {
  target.webContents.setWindowOpenHandler(() => ({ action: "deny" }));
}

function resolvePathFromOpenArgument(value: string): string | null {
  const trimmed = value.trim();
  if (trimmed.length === 0 || trimmed.startsWith("-")) {
    return null;
  }

  if (trimmed.startsWith("file://")) {
    try {
      return fileURLToPath(trimmed);
    } catch {
      return null;
    }
  }

  if (/^[a-z][a-z0-9+.-]*:\/\//i.test(trimmed)) {
    return null;
  }

  return trimmed;
}

async function resolvePlayablePaths(paths: string[]): Promise<string[]> {
  const resolved: string[] = [];
  const seen = new Set<string>();

  for (const value of paths) {
    const openPath = resolvePathFromOpenArgument(value);
    if (!openPath) {
      continue;
    }

    const normalized = path.resolve(openPath);
    if (seen.has(normalized)) {
      continue;
    }

    try {
      const stat = await fs.stat(normalized);
      if (stat.isDirectory() || (stat.isFile() && isAudioFile(normalized))) {
        seen.add(normalized);
        resolved.push(normalized);
      }
    } catch {
      // ignore
    }
  }

  return resolved;
}

function getOpenTargetArgsFromArgv(argv: string[]): string[] {
  const argOffset = process.defaultApp ? 2 : 1;
  return argv.slice(argOffset);
}

async function replacePlaylistWithPathsAndPlay(paths: string[]): Promise<void> {
  const uniquePaths = [...new Set(paths.map((entry) => path.resolve(entry)))];
  if (uniquePaths.length === 0) {
    return;
  }

  await withController(async (target) => {
    if (uniquePaths.length === 1) {
      const firstPath = uniquePaths[0];
      if (firstPath) {
        await target.handlePlaylistCommand({ type: "replaceFromPathAndPlay", path: firstPath });
      }
      return;
    }

    await target.handlePlaylistCommand({ type: "clear" });
    await target.handlePlaylistCommand({ type: "addPaths", paths: uniquePaths });
    await target.handlePlaybackCommand({ type: "play" });
  });
}

async function replacePlaylistFromExternalTargetsAndPlay(paths: string[]): Promise<void> {
  const uniquePaths = [...new Set(paths.map((entry) => path.resolve(entry)))];
  if (uniquePaths.length === 0) {
    return;
  }

  const firstAudioFilePath = uniquePaths.find((entry) => isAudioFile(entry));
  if (firstAudioFilePath) {
    await withController(async (target) => {
      await target.handlePlaylistCommand({ type: "replaceFromPathAndPlay", path: firstAudioFilePath });
    });
    return;
  }

  await replacePlaylistWithPathsAndPlay(uniquePaths);
}

async function addPathsToPlaylist(paths: string[]): Promise<void> {
  const uniquePaths = [...new Set(paths.map((entry) => path.resolve(entry)))];
  if (uniquePaths.length === 0) {
    return;
  }

  await withController(async (target) => {
    await target.handlePlaylistCommand({ type: "addPaths", paths: uniquePaths });
  });
}

async function getDialogDefaultPath(): Promise<string | undefined> {
  try {
    const settings = await withController(async (target) => target.getSettings());
    return settings.musicRoot;
  } catch {
    return undefined;
  }
}

async function showOpenDialog(options: Electron.OpenDialogOptions): Promise<string[]> {
  const owner = mainWindow && !mainWindow.isDestroyed() ? mainWindow : undefined;
  const result = owner ? await dialog.showOpenDialog(owner, options) : await dialog.showOpenDialog(options);
  return result.canceled ? [] : result.filePaths;
}

async function openAudioFilesFromDialogAndPlay(): Promise<void> {
  const defaultPath = await getDialogDefaultPath();
  const extensions = [...SUPPORTED_AUDIO_EXTENSIONS].map((entry) => entry.slice(1)).sort((a, b) => a.localeCompare(b));
  const selected = await showOpenDialog({
    title: "Open Audio Files",
    properties: ["openFile", "multiSelections"],
    ...(defaultPath ? { defaultPath } : {}),
    filters: [
      { name: "Audio Files", extensions },
      { name: "All Files", extensions: ["*"] }
    ]
  });
  if (selected.length === 0) {
    return;
  }

  await replacePlaylistWithPathsAndPlay(selected);
}

async function addAudioFilesFromDialog(): Promise<void> {
  const defaultPath = await getDialogDefaultPath();
  const extensions = [...SUPPORTED_AUDIO_EXTENSIONS].map((entry) => entry.slice(1)).sort((a, b) => a.localeCompare(b));
  const selected = await showOpenDialog({
    title: "Add Audio Files",
    properties: ["openFile", "multiSelections"],
    ...(defaultPath ? { defaultPath } : {}),
    filters: [
      { name: "Audio Files", extensions },
      { name: "All Files", extensions: ["*"] }
    ]
  });
  if (selected.length === 0) {
    return;
  }

  await addPathsToPlaylist(selected);
}

async function openDirectoriesFromDialogAndPlay(): Promise<void> {
  const defaultPath = await getDialogDefaultPath();
  const selected = await showOpenDialog({
    title: "Open Directory",
    properties: ["openDirectory", "multiSelections"],
    ...(defaultPath ? { defaultPath } : {})
  });
  if (selected.length === 0) {
    return;
  }

  await replacePlaylistWithPathsAndPlay(selected);
}

async function addDirectoriesFromDialog(): Promise<void> {
  const defaultPath = await getDialogDefaultPath();
  const selected = await showOpenDialog({
    title: "Add Directory",
    properties: ["openDirectory", "multiSelections"],
    ...(defaultPath ? { defaultPath } : {})
  });
  if (selected.length === 0) {
    return;
  }

  await addPathsToPlaylist(selected);
}

function queueExternalOpenTargetBatch(paths: string[]): void {
  if (paths.length === 0) {
    return;
  }
  pendingExternalOpenTargetBatches.push(paths);
}

async function flushPendingExternalOpenTargetBatches(): Promise<void> {
  while (pendingExternalOpenTargetBatches.length > 0) {
    const nextBatch = pendingExternalOpenTargetBatches.shift();
    if (!nextBatch || nextBatch.length === 0) {
      continue;
    }

    try {
      await replacePlaylistFromExternalTargetsAndPlay(nextBatch);
    } catch (error) {
      console.error("Failed to process queued open request:", error);
    }
  }
}

async function handleExternalOpenRequest(rawTargets: string[]): Promise<void> {
  const targets = await resolvePlayablePaths(rawTargets);
  if (targets.length === 0) {
    return;
  }

  if (!controllerInitPromise && !controller) {
    queueExternalOpenTargetBatch(targets);
    return;
  }

  try {
    await replacePlaylistFromExternalTargetsAndPlay(targets);
  } catch (error) {
    console.error("Failed to handle external open request:", error);
    queueExternalOpenTargetBatch(targets);
  }
}

function buildAppMenu(): void {
  const viewSubmenu: MenuItemConstructorOptions[] = [
    { label: "Focus File Browser", click: () => dispatchMenuAction("focus-browser") },
    { label: "Focus Playlist", click: () => dispatchMenuAction("focus-playlist") },
    { type: "separator" },
    { role: "reload" },
    { role: "forceReload" },
    { role: "togglefullscreen" }
  ];

  if (isDevelopmentMode) {
    viewSubmenu.push({ role: "toggleDevTools" });
  }

  const template: MenuItemConstructorOptions[] = [
    {
      label: "File",
      submenu: [
        {
          label: "Open...",
          accelerator: "CommandOrControl+O",
          click: () => {
            void openAudioFilesFromDialogAndPlay();
          }
        },
        {
          label: "Add...",
          accelerator: "CommandOrControl+Shift+O",
          click: () => {
            void addAudioFilesFromDialog();
          }
        },
        { type: "separator" },
        {
          label: "Open Directory...",
          click: () => {
            void openDirectoriesFromDialogAndPlay();
          }
        },
        {
          label: "Add Directory...",
          click: () => {
            void addDirectoriesFromDialog();
          }
        },
        { type: "separator" },
        { label: "Add Selected Path", click: () => dispatchMenuAction("add-selected-path") },
        { label: "Replace From Selected Path", click: () => dispatchMenuAction("replace-selected-path") },
        { type: "separator" },
        { label: "Settings", accelerator: "CommandOrControl+,", click: () => dispatchMenuAction("open-settings") },
        { type: "separator" },
        { role: "quit", accelerator: "CommandOrControl+Q" }
      ]
    },
    {
      label: "Playback",
      submenu: [
        { label: "Play/Pause", click: () => void dispatchPlaybackCommand({ type: "playPause" }) },
        { label: "Stop", click: () => void dispatchPlaybackCommand({ type: "stop" }) },
        { label: "Previous", click: () => void dispatchPlaybackCommand({ type: "previous" }) },
        { label: "Next", click: () => void dispatchPlaybackCommand({ type: "next" }) },
        { type: "separator" },
        { label: "Toggle Shuffle", click: () => void dispatchPlaybackCommand({ type: "toggleShuffle" }) },
        { label: "Cycle Repeat Mode", click: () => void dispatchPlaybackCommand({ type: "cycleRepeat" }) }
      ]
    },
    {
      label: "Playlist",
      submenu: [
        { label: "Remove Selected Track", click: () => dispatchMenuAction("remove-selected-track") },
        { label: "Clear Playlist", click: () => dispatchMenuAction("clear-playlist") },
        { type: "separator" },
        { label: "Sort by Track #", click: () => dispatchMenuAction("sort-track") },
        { label: "Sort by Album", click: () => dispatchMenuAction("sort-album") },
        { label: "Sort by Title", click: () => dispatchMenuAction("sort-title") },
        { label: "Sort by Length", click: () => dispatchMenuAction("sort-length") },
        { label: "Sort by Codec", click: () => dispatchMenuAction("sort-codec") },
        { label: "Sort by Bitrate", click: () => dispatchMenuAction("sort-bitrate") },
        { label: "Sort by ReplayGain", click: () => dispatchMenuAction("sort-replaygain") },
        { label: "Sort by DR", click: () => dispatchMenuAction("sort-plr") }
      ]
    },
    {
      label: "View",
      submenu: viewSubmenu
    },
    {
      label: "Window",
      submenu: [
        { role: "minimize" },
        { role: "close" }
      ]
    }
  ];

  Menu.setApplicationMenu(Menu.buildFromTemplate(template));
}

async function loadRendererView(target: BrowserWindow, query: Record<string, string>): Promise<void> {
  const rendererUrl = process.env.ELECTRON_RENDERER_URL;
  if (rendererUrl) {
    const url = new URL(rendererUrl);
    for (const [key, value] of Object.entries(query)) {
      url.searchParams.set(key, value);
    }
    await target.loadURL(url.toString());
    return;
  }

  const htmlPath = path.join(__dirname, "..", "..", "..", "dist-renderer", "index.html");
  await target.loadFile(htmlPath, { query });
}

function createMainWindow(): void {
  mainWindow = new BrowserWindow({
    width: 1200,
    height: 700,
    minWidth: 900,
    minHeight: 500,
    title: "Impulse",
    autoHideMenuBar: false,
    icon: appIconPath,
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      devTools: isDevelopmentMode,
      preload: path.join(__dirname, "..", "..", "..", "dist", "preload", "preload", "index.js")
    }
  });

  const rendererUrl = process.env.ELECTRON_RENDERER_URL;
  if (rendererUrl) {
    void mainWindow.loadURL(rendererUrl);
  } else {
    const htmlPath = path.join(__dirname, "..", "..", "..", "dist-renderer", "index.html");
    void mainWindow.loadFile(htmlPath);
  }

  mainWindow.on("closed", () => {
    mainWindow = null;
  });
  applyWindowNavigationGuards(mainWindow);

  if (isDevelopmentMode) {
    mainWindow.webContents.on("before-input-event", (event, input) => {
      const key = input.key.toLowerCase();
      const openDevtoolsShortcut = key === "f12" || (input.control && input.shift && key === "i");
      if (openDevtoolsShortcut) {
        mainWindow?.webContents.openDevTools({ mode: "detach" });
        event.preventDefault();
      }
    });
  }

  buildAppMenu();
}

async function openSettingsWindow(): Promise<void> {
  if (settingsWindow && !settingsWindow.isDestroyed()) {
    settingsWindow.show();
    settingsWindow.focus();
    return;
  }

  const parentWindow = mainWindow && !mainWindow.isDestroyed() ? mainWindow : null;

  settingsWindow = new BrowserWindow({
    width: 700,
    height: 600,
    minWidth: 500,
    minHeight: 500,
    title: "Impulse - Settings",
    autoHideMenuBar: true,
    icon: appIconPath,
    ...(parentWindow ? { parent: parentWindow } : {}),
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      devTools: isDevelopmentMode,
      preload: path.join(__dirname, "..", "..", "..", "dist", "preload", "preload", "index.js")
    }
  });

  settingsWindow.on("closed", () => {
    settingsWindow = null;
  });
  applyWindowNavigationGuards(settingsWindow);

  await loadRendererView(settingsWindow, { view: "settings" });
}

async function openFilePropertiesWindow(requestedPath: string): Promise<void> {
  const normalizedPath = path.resolve(requestedPath);
  const windowTitle = `File Properties - ${path.basename(normalizedPath)}`;

  if (filePropertiesWindow && !filePropertiesWindow.isDestroyed()) {
    filePropertiesWindow.setTitle(windowTitle);
    await loadRendererView(filePropertiesWindow, {
      view: "file-properties",
      path: normalizedPath
    });
    filePropertiesWindow.show();
    filePropertiesWindow.focus();
    return;
  }

  const parentWindow = mainWindow && !mainWindow.isDestroyed() ? mainWindow : null;

  filePropertiesWindow = new BrowserWindow({
    width: 900,
    height: 700,
    minWidth: 700,
    minHeight: 500,
    title: windowTitle,
    autoHideMenuBar: true,
    icon: appIconPath,
    ...(parentWindow ? { parent: parentWindow } : {}),
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      devTools: isDevelopmentMode,
      preload: path.join(__dirname, "..", "..", "..", "dist", "preload", "preload", "index.js")
    }
  });

  filePropertiesWindow.on("closed", () => {
    filePropertiesWindow = null;
  });
  applyWindowNavigationGuards(filePropertiesWindow);

  await loadRendererView(filePropertiesWindow, {
    view: "file-properties",
    path: normalizedPath
  });
}

async function openAlbumArtWindow(requestedPath: string): Promise<void> {
  const normalizedPath = path.resolve(requestedPath);
  const windowTitle = `Album Art - ${path.basename(normalizedPath)}`;

  if (albumArtWindow && !albumArtWindow.isDestroyed()) {
    albumArtWindow.setTitle(windowTitle);
    await loadRendererView(albumArtWindow, {
      view: "album-art",
      path: normalizedPath
    });
    albumArtWindow.show();
    albumArtWindow.focus();
    return;
  }

  const parentWindow = mainWindow && !mainWindow.isDestroyed() ? mainWindow : null;

  albumArtWindow = new BrowserWindow({
    width: 512,
    height: 514,
    minWidth: 256,
    minHeight: 256,
    resizable: true,
    title: windowTitle,
    autoHideMenuBar: true,
    icon: appIconPath,
    ...(parentWindow ? { parent: parentWindow } : {}),
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      devTools: isDevelopmentMode,
      preload: path.join(__dirname, "..", "..", "..", "dist", "preload", "preload", "index.js")
    }
  });

  albumArtWindow.on("closed", () => {
    albumArtWindow = null;
  });
  applyWindowNavigationGuards(albumArtWindow);

  await loadRendererView(albumArtWindow, {
    view: "album-art",
    path: normalizedPath
  });
}

async function withController<T>(handler: (target: AppController) => Promise<T>): Promise<T> {
  if (controllerInitPromise) {
    await controllerInitPromise.catch(() => undefined);
  }

  if (!controller) {
    throw new Error("Controller not initialized");
  }

  return await handler(controller);
}

function registerIpcHandlers(): void {
  ipcMain.handle("app:init", async (event: IpcMainInvokeEvent) => {
    assertTrustedIpcSender(event);
    return await withController(async (target) => {
      return target.getInitialState();
    });
  });

  ipcMain.handle("settings:get", async (event: IpcMainInvokeEvent) => {
    assertTrustedIpcSender(event);
    return await withController(async (target) => target.getSettings());
  });

  ipcMain.handle("settings:save", async (event: IpcMainInvokeEvent, nextSettings: AppSettings) => {
    assertTrustedIpcSender(event);
    return await withController(async (target) => await target.saveSettings(nextSettings));
  });

  ipcMain.handle("browser:listEntries", async (event: IpcMainInvokeEvent, requestedPath: string) => {
    assertTrustedIpcSender(event);
    await withController(async (target) => {
      await target.listBrowserEntries(requestedPath);
    });
  });

  ipcMain.handle("playback:command", async (event: IpcMainInvokeEvent, command: PlaybackCommand) => {
    assertTrustedIpcSender(event);
    await withController(async (target) => {
      await target.handlePlaybackCommand(command);
    });
  });

  ipcMain.handle("playlist:command", async (event: IpcMainInvokeEvent, command: PlaylistCommand) => {
    assertTrustedIpcSender(event);
    await withController(async (target) => {
      await target.handlePlaylistCommand(command);
    });
  });

  ipcMain.handle("coverArt:readAsDataUrl", async (event: IpcMainInvokeEvent, imagePath: string) => {
    assertTrustedIpcSender(event);
    return await withController(async (target) => await target.readCoverArtAsDataUrl(imagePath));
  });

  ipcMain.handle("file:properties", async (event: IpcMainInvokeEvent, requestedPath: string) => {
    assertTrustedIpcSender(event);
    return await withController(async (target) => await target.getFileProperties(requestedPath));
  });

  ipcMain.handle("window:openSettings", async (event: IpcMainInvokeEvent) => {
    assertTrustedIpcSender(event);
    await openSettingsWindow();
  });

  ipcMain.handle("window:openFileProperties", async (event: IpcMainInvokeEvent, requestedPath: string) => {
    assertTrustedIpcSender(event);
    await openFilePropertiesWindow(requestedPath);
  });

  ipcMain.handle("window:openAlbumArt", async (event: IpcMainInvokeEvent, requestedPath: string) => {
    assertTrustedIpcSender(event);
    await openAlbumArtWindow(requestedPath);
  });
}

async function dispatchPlaybackCommand(command: PlaybackCommand): Promise<void> {
  if (controllerInitPromise) {
    await controllerInitPromise.catch(() => undefined);
  }

  if (!controller) {
    return;
  }

  try {
    await controller.handlePlaybackCommand(command);
  } catch (error) {
    console.error("Playback command dispatch failed:", error);
  }
}

async function shutdownController(): Promise<void> {
  if (controllerShutdownPromise) {
    return controllerShutdownPromise;
  }

  controllerShutdownPromise = (async () => {
    if (controllerInitPromise) {
      await controllerInitPromise.catch(() => undefined);
    }

    if (controller) {
      try {
        await controller.shutdown();
      } catch (error) {
        console.error("Controller shutdown failed:", error);
      }
    }

    mprisBridge?.shutdown();
    mprisBridge = null;
  })();

  await controllerShutdownPromise;
}

async function bootstrap(): Promise<void> {
  await app.whenReady();
  mprisBridge = new MprisBridge({
    dispatchPlaybackCommand,
    focusMainWindow,
    quitApp() {
      app.quit();
    }
  });
  registerIpcHandlers();
  createMainWindow();
  void handleExternalOpenRequest(getOpenTargetArgsFromArgv(process.argv));

  try {
    controllerInitPromise = (async () => {
      controller = new AppController({
        emit(event) {
          broadcastEvent("app:event", event);
          mprisBridge?.handleAppEvent(event);
        }
      });
      initialState = await controller.init();
      mprisBridge?.applyInitialState(initialState);
    })();

    await controllerInitPromise;
    await flushPendingExternalOpenTargetBatches();
  } catch (error) {
    console.error("Failed to initialize app controller:", error);
  }

  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createMainWindow();
    }
  });
}

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") {
    app.quit();
  }
});

app.on("before-quit", (event) => {
  if (quitAfterShutdown) {
    return;
  }

  event.preventDefault();
  void shutdownController().finally(() => {
    quitAfterShutdown = true;
    app.quit();
  });
});

if (!hasSingleInstanceLock) {
  app.quit();
} else {
  app.on("second-instance", (_event, argv) => {
    if (app.isReady()) {
      focusMainWindow();
    }
    void handleExternalOpenRequest(getOpenTargetArgsFromArgv(argv));
  });

  app.on("open-file", (event, filePath) => {
    event.preventDefault();
    if (app.isReady()) {
      focusMainWindow();
    }
    void handleExternalOpenRequest([filePath]);
  });

  void bootstrap().catch((error) => {
    console.error("Fatal bootstrap failure:", error);
  });
}
