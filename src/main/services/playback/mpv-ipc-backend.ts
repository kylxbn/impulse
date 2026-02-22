import net from "node:net";
import path from "node:path";
import { EventEmitter } from "node:events";
import { spawn, type ChildProcessWithoutNullStreams } from "node:child_process";
import { randomUUID } from "node:crypto";
import { promises as fs } from "node:fs";
import { app } from "electron";
import type {
  PlaybackAudioParams,
  PlaybackBackend,
  PlaybackBackendEvent,
  PlaybackBackendState
} from "./backend.js";

interface MpvResponse {
  request_id?: number;
  error?: string;
  data?: unknown;
  event?: string;
  name?: string;
  reason?: string;
}

interface PendingRequestHandlers {
  resolve: (value: unknown) => void;
  reject: (reason?: unknown) => void;
  timeout: NodeJS.Timeout;
}

const MPV_COMMAND_TIMEOUT_MS = 5000;

const OBSERVE_PROPERTIES: Array<{ id: number; name: string }> = [
  { id: 1, name: "pause" },
  { id: 2, name: "time-pos" },
  { id: 3, name: "duration" },
  { id: 4, name: "volume" },
  { id: 5, name: "audio-bitrate" },
  { id: 6, name: "audio-codec-name" },
  { id: 7, name: "file-format" },
  { id: 8, name: "audio-params" },
  { id: 9, name: "audio-out-params" },
  { id: 10, name: "current-ao" },
  { id: 11, name: "volume-gain" }
];

function parseString(value: unknown): string | null {
  if (typeof value !== "string") {
    return null;
  }

  const trimmed = value.trim();
  return trimmed.length > 0 ? trimmed : null;
}

function parsePositiveInteger(value: unknown): number | null {
  if (typeof value !== "number" || !Number.isFinite(value) || value <= 0) {
    return null;
  }

  return Math.round(value);
}

function parseNumber(value: unknown): number | null {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    return null;
  }

  return value;
}

function parseAudioParams(value: unknown): PlaybackAudioParams | null {
  if (!value || typeof value !== "object") {
    return null;
  }

  const params = value as Record<string, unknown>;
  const format = parseString(params.format);
  const sampleRateHz = parsePositiveInteger(params.samplerate);
  const channels = parseString(params.channels);
  const channelCount = (
    parsePositiveInteger(params["channel-count"])
    ?? parsePositiveInteger(params.channel_count)
  );

  if (!format && sampleRateHz == null && !channels && channelCount == null) {
    return null;
  }

  return {
    format,
    sampleRateHz,
    channels,
    channelCount
  };
}

export class MpvIpcBackend implements PlaybackBackend {
  private readonly events = new EventEmitter();
  private readonly socketPath: string;
  private mpvProcess: ChildProcessWithoutNullStreams | null = null;
  private socket: net.Socket | null = null;
  private connected = false;
  private responseBuffer = "";
  private nextRequestId = 1;
  private pending = new Map<number, PendingRequestHandlers>();
  private currentState: PlaybackBackendState = {
    paused: true,
    timePosSec: 0,
    durationSec: null,
    volumePercent: 100,
    audioBitrateKbps: null,
    audioCodecName: null,
    fileFormat: null,
    audioParams: null,
    audioOutParams: null,
    currentAo: null,
    volumeGainDb: null
  };
  private shuttingDown = false;

  public constructor() {
    const tempDir = app.getPath("temp");
    this.socketPath = path.join(tempDir, `impulse-mpv-${randomUUID()}.sock`);
  }

  public async start(): Promise<void> {
    this.shuttingDown = false;
    await this.removeSocketIfNeeded();

    this.mpvProcess = spawn("mpv", [
      "--idle=yes",
      "--no-terminal",
      "--force-window=no",
      "--audio-display=no",
      "--msg-level=all=warn",
      "--really-quiet",
      `--input-ipc-server=${this.socketPath}`
    ]);

    this.mpvProcess.on("error", (error) => {
      this.rejectAllPending(new Error(`mpv process error: ${error.message}`));
      this.emit({ type: "error", message: `mpv process error: ${error.message}` });
    });

    this.mpvProcess.on("exit", (code, signal) => {
      this.connected = false;
      this.rejectAllPending(new Error("mpv exited before replying to pending command(s)."));
      if (this.shuttingDown) {
        return;
      }
      this.emit({
        type: "error",
        message: `mpv exited unexpectedly (code=${code ?? "n/a"}, signal=${signal ?? "n/a"})`
      });
    });

    await this.connectSocketWithRetry();
    await this.observeProperties();
  }

  public async stop(): Promise<void> {
    this.shuttingDown = true;

    if (this.connected) {
      try {
        await Promise.race([
          this.sendCommand(["quit"]).catch(() => undefined),
          new Promise<void>((resolve) => setTimeout(resolve, 500))
        ]);
      } catch {
        // welp
      }
    }

    this.rejectAllPending(new Error("mpv request canceled during shutdown."));

    if (this.socket) {
      this.socket.destroy();
      this.socket = null;
    }
    this.connected = false;

    if (this.mpvProcess) {
      this.mpvProcess.kill("SIGTERM");
      this.mpvProcess = null;
    }

    await this.removeSocketIfNeeded();
  }

  public async loadFile(filePath: string): Promise<void> {
    await this.sendCommand(["loadfile", filePath, "replace"]);
  }

  public async play(): Promise<void> {
    await this.sendCommand(["set_property", "pause", false]);
  }

  public async pause(): Promise<void> {
    await this.sendCommand(["set_property", "pause", true]);
  }

  public async togglePause(): Promise<void> {
    await this.sendCommand(["cycle", "pause"]);
  }

  public async seekRelative(seconds: number): Promise<void> {
    await this.sendCommand(["seek", seconds, "relative"]);
  }

  public async seekAbsolute(seconds: number): Promise<void> {
    await this.sendCommand(["seek", seconds, "absolute"]);
  }

  public async setVolume(percent: number): Promise<void> {
    await this.sendCommand(["set_property", "volume", percent]);
  }

  public async setReplayGain(taggedDb: number, untaggedDb: number): Promise<void> {
    await this.setPropertyWithFallback(["options/replaygain", "replaygain"], "track", true);
    await this.setPropertyWithFallback(
      ["options/replaygain-preamp", "replaygain-preamp"],
      taggedDb,
      true
    );
    await this.setPropertyWithFallback(
      [
        "options/replaygain-fallback",
        "replaygain-fallback",
        "options/replaygain-missing-preamp",
        "replaygain-missing-preamp"
      ],
      untaggedDb,
      false
    );
  }

  public subscribe(listener: (event: PlaybackBackendEvent) => void): () => void {
    this.events.on("event", listener);
    return () => {
      this.events.off("event", listener);
    };
  }

  private emit(event: PlaybackBackendEvent): void {
    this.events.emit("event", event);
  }

  private async connectSocketWithRetry(): Promise<void> {
    const maxAttempts = 100;
    for (let attempt = 1; attempt <= maxAttempts; attempt += 1) {
      try {
        await this.tryConnectSocket();
        this.connected = true;
        return;
      } catch {
        await new Promise((resolve) => setTimeout(resolve, 50));
      }
    }

    throw new Error("Failed to connect to mpv IPC socket.");
  }

  private async tryConnectSocket(): Promise<void> {
    await new Promise<void>((resolve, reject) => {
      const socket = net.createConnection(this.socketPath);

      socket.once("error", (error) => {
        socket.destroy();
        reject(error);
      });

      socket.once("connect", () => {
        this.socket = socket;
        socket.setEncoding("utf8");

        socket.on("data", (chunk) => {
          this.onSocketData(chunk.toString());
        });

        socket.on("close", () => {
          this.connected = false;
          this.rejectAllPending(new Error("mpv IPC socket closed."));
        });

        socket.on("error", (error) => {
          this.rejectAllPending(new Error(`mpv socket error: ${error.message}`));
          this.emit({ type: "error", message: `mpv socket error: ${error.message}` });
        });

        resolve();
      });
    });
  }

  private onSocketData(chunk: string): void {
    this.responseBuffer += chunk;

    while (true) {
      const newlineIndex = this.responseBuffer.indexOf("\n");
      if (newlineIndex === -1) {
        break;
      }

      const line = this.responseBuffer.slice(0, newlineIndex).trim();
      this.responseBuffer = this.responseBuffer.slice(newlineIndex + 1);

      if (!line) {
        continue;
      }

      this.handleMessage(line);
    }
  }

  private handleMessage(line: string): void {
    let parsed: MpvResponse;
    try {
      parsed = JSON.parse(line) as MpvResponse;
    } catch {
      return;
    }

    if (typeof parsed.request_id === "number") {
      const handlers = this.pending.get(parsed.request_id);
      if (handlers) {
        this.pending.delete(parsed.request_id);
        clearTimeout(handlers.timeout);
        if (parsed.error && parsed.error !== "success") {
          handlers.reject(new Error(parsed.error));
        } else {
          handlers.resolve(parsed.data);
        }
      }
      return;
    }

    if (parsed.event === "property-change") {
      const nextState: Partial<PlaybackBackendState> = {};
      switch (parsed.name) {
        case "pause":
          nextState.paused = Boolean(parsed.data);
          break;
        case "time-pos":
          nextState.timePosSec = Number(parsed.data ?? 0);
          break;
        case "duration":
          nextState.durationSec = parsed.data == null ? null : Number(parsed.data);
          break;
        case "volume":
          nextState.volumePercent = Number(parsed.data ?? 100);
          break;
        case "audio-bitrate":
          nextState.audioBitrateKbps =
            parsed.data == null ? null : Math.round(Number(parsed.data) / 1000) || null;
          break;
        case "audio-codec-name":
          nextState.audioCodecName = parseString(parsed.data);
          break;
        case "file-format":
          nextState.fileFormat = parseString(parsed.data);
          break;
        case "audio-params":
          nextState.audioParams = parseAudioParams(parsed.data);
          break;
        case "audio-out-params":
          nextState.audioOutParams = parseAudioParams(parsed.data);
          break;
        case "current-ao":
          nextState.currentAo = parseString(parsed.data);
          break;
        case "volume-gain":
          nextState.volumeGainDb = parseNumber(parsed.data);
          break;
        default:
          break;
      }

      if (Object.keys(nextState).length > 0) {
        this.currentState = {
          ...this.currentState,
          ...nextState
        };
        this.emit({ type: "state", payload: nextState });
      }
      return;
    }

    if (parsed.event === "file-loaded") {
      this.emit({ type: "fileLoaded" });
      return;
    }

    if (parsed.event === "end-file") {
      this.emit({ type: "endOfFile", reason: parsed.reason ?? "unknown" });
    }
  }

  private async observeProperties(): Promise<void> {
    for (const property of OBSERVE_PROPERTIES) {
      await this.sendCommand(["observe_property", property.id, property.name]);
    }
  }

  private async setPropertyWithFallback(
    propertyNames: string[],
    value: string | number,
    strict: boolean
  ): Promise<void> {
    let lastOptionError: Error | null = null;

    for (const propertyName of propertyNames) {
      try {
        await this.sendCommand(["set_property", propertyName, value]);
        return;
      } catch (error) {
        if (!this.isOptionCompatibilityError(error)) {
          throw error;
        }
        lastOptionError = error as Error;
      }
    }

    if (strict && lastOptionError) {
      throw lastOptionError;
    }
  }

  private isOptionCompatibilityError(error: unknown): boolean {
    const message = (error as Error)?.message?.toLowerCase?.() ?? "";
    return message.includes("error running command")
      || message.includes("property unavailable")
      || message.includes("unknown property")
      || message.includes("option");
  }

  private async sendCommand(command: unknown[]): Promise<unknown> {
    if (!this.connected || !this.socket) {
      throw new Error("mpv IPC socket is not connected.");
    }

    const requestId = this.nextRequestId;
    this.nextRequestId += 1;

    const payload = JSON.stringify({ command, request_id: requestId }) + "\n";

    await new Promise<void>((resolve, reject) => {
      this.socket?.write(payload, (error) => {
        if (error) {
          reject(error);
          return;
        }
        resolve();
      });
    });

    const commandLabel = typeof command[0] === "string" ? command[0] : "unknown";
    return await new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        const pending = this.pending.get(requestId);
        if (!pending) {
          return;
        }
        this.pending.delete(requestId);
        pending.reject(new Error(`mpv command timed out: ${commandLabel}`));
      }, MPV_COMMAND_TIMEOUT_MS);

      this.pending.set(requestId, { resolve, reject, timeout });
    });
  }

  private rejectAllPending(reason: Error): void {
    for (const handlers of this.pending.values()) {
      clearTimeout(handlers.timeout);
      handlers.reject(reason);
    }
    this.pending.clear();
  }

  private async removeSocketIfNeeded(): Promise<void> {
    try {
      await fs.unlink(this.socketPath);
    } catch {
      // give up
    }
  }
}
