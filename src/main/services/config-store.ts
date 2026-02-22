import path from "node:path";
import os from "node:os";
import { promises as fs } from "node:fs";
import { app } from "electron";
import { DEFAULT_SETTINGS } from "../../shared/constants.js";
import type { AppSettings } from "../../shared/types.js";
import { sanitizeAppSettings } from "./settings-utils.js";

const SETTINGS_FILE = "config.json";

export class ConfigStore {
  private readonly configPath: string;
  private readonly defaults: AppSettings;

  public constructor() {
    this.configPath = path.join(app.getPath("userData"), SETTINGS_FILE);
    this.defaults = sanitizeAppSettings(
      {
        ...DEFAULT_SETTINGS,
        musicRoot: path.join(os.homedir(), "Music")
      },
      {
        ...DEFAULT_SETTINGS,
        musicRoot: path.join(os.homedir(), "Music")
      }
    );
  }

  public getDefaults(): AppSettings {
    return {
      ...this.defaults
    };
  }

  public async load(): Promise<AppSettings> {
    try {
      const data = await fs.readFile(this.configPath, "utf8");
      const parsed = JSON.parse(data) as Partial<AppSettings>;
      return sanitizeAppSettings(parsed, this.defaults);
    } catch {
      return this.getDefaults();
    }
  }

  public async save(next: AppSettings): Promise<void> {
    const validated = sanitizeAppSettings(next, this.defaults);
    await fs.mkdir(path.dirname(this.configPath), { recursive: true });
    await fs.writeFile(this.configPath, JSON.stringify(validated, null, 2), "utf8");
  }

  public getPath(): string {
    return this.configPath;
  }
}
