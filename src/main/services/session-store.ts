import path from "node:path";
import { promises as fs } from "node:fs";
import { app } from "electron";
import type { SessionState } from "../../shared/types.js";

const SESSION_FILE = "session.json";

export class SessionStore {
  private sessionPath: string;
  private writeQueue: Promise<void> = Promise.resolve();

  public constructor() {
    this.sessionPath = path.join(app.getPath("userData"), SESSION_FILE);
  }

  public async load(): Promise<SessionState | null> {
    try {
      const data = await fs.readFile(this.sessionPath, "utf8");
      return JSON.parse(data) as SessionState;
    } catch {
      return null;
    }
  }

  public async save(state: SessionState): Promise<void> {
    this.writeQueue = this.writeQueue.then(async () => {
      const dir = path.dirname(this.sessionPath);
      const tempPath = `${this.sessionPath}.${process.pid}.${Date.now()}.tmp`;

      await fs.mkdir(dir, { recursive: true });

      try {
        await fs.writeFile(tempPath, JSON.stringify(state, null, 2), "utf8");
        await fs.rename(tempPath, this.sessionPath);
      } catch (error) {
        try {
          await fs.unlink(tempPath);
        } catch {
          // give up if we can't clean up
        }
        throw error;
      }
    });

    await this.writeQueue;
  }
}
