export type MetadataLoadPriority = "high" | "normal";

export interface MetadataLoadRequest {
  trackId: string;
  filePath: string;
}

interface MetadataLoadTask extends MetadataLoadRequest {
  resolve(): void;
}

interface MetadataLoadQueueOptions {
  concurrency: number;
  runTask(task: MetadataLoadRequest): Promise<void>;
}

export class MetadataLoadQueue {
  private readonly highQueue: MetadataLoadTask[] = [];
  private readonly normalQueue: MetadataLoadTask[] = [];
  private readonly inFlightByTrack = new Map<string, Promise<void>>();
  private readonly concurrency: number;
  private readonly runTask: (task: MetadataLoadRequest) => Promise<void>;
  private workerCount = 0;
  private shuttingDown = false;

  public constructor(options: MetadataLoadQueueOptions) {
    this.concurrency = Math.max(1, Math.floor(options.concurrency));
    this.runTask = options.runTask;
  }

  public enqueue(request: MetadataLoadRequest, priority: MetadataLoadPriority = "normal"): Promise<void> {
    if (this.shuttingDown) {
      return Promise.resolve();
    }

    const existing = this.inFlightByTrack.get(request.trackId);
    if (existing) {
      if (priority === "high") {
        this.promote(request.trackId);
      }
      return existing;
    }

    const taskPromise = new Promise<void>((resolve) => {
      const task: MetadataLoadTask = {
        ...request,
        resolve
      };

      if (priority === "high") {
        this.highQueue.push(task);
      } else {
        this.normalQueue.push(task);
      }

      this.drain();
    }).finally(() => {
      this.inFlightByTrack.delete(request.trackId);
    });

    this.inFlightByTrack.set(request.trackId, taskPromise);
    return taskPromise;
  }

  public shutdown(): void {
    this.shuttingDown = true;

    const pendingTasks = [...this.highQueue, ...this.normalQueue];
    this.highQueue.length = 0;
    this.normalQueue.length = 0;

    for (const task of pendingTasks) {
      task.resolve();
    }
  }

  private drain(): void {
    if (this.shuttingDown) {
      return;
    }

    while (this.workerCount < this.concurrency) {
      const task = this.shiftNextTask();
      if (!task) {
        return;
      }

      this.workerCount += 1;
      void this.runTask(task).catch(() => {
        // just in case
      }).finally(() => {
        this.workerCount -= 1;
        task.resolve();
        this.drain();
      });
    }
  }

  private shiftNextTask(): MetadataLoadTask | null {
    if (this.highQueue.length > 0) {
      return this.highQueue.shift() ?? null;
    }

    if (this.normalQueue.length > 0) {
      return this.normalQueue.shift() ?? null;
    }

    return null;
  }

  private promote(trackId: string): void {
    const index = this.normalQueue.findIndex((task) => task.trackId === trackId);
    if (index === -1) {
      return;
    }

    const [task] = this.normalQueue.splice(index, 1);
    if (task) {
      this.highQueue.push(task);
    }
  }
}
