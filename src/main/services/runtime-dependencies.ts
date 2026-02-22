import { execFile } from "node:child_process";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);

interface RuntimeDependency {
  command: string;
  args: string[];
  required: boolean;
  label: string;
}

export interface RuntimeDependencyReport {
  missingRequired: string[];
  missingOptional: string[];
}

const DEPENDENCIES: RuntimeDependency[] = [
  {
    command: "mpv",
    args: ["--version"],
    required: true,
    label: "mpv"
  },
  {
    command: "ffprobe",
    args: ["-version"],
    required: false,
    label: "ffprobe"
  }
];

async function isCommandAvailable(command: string, args: string[]): Promise<boolean> {
  try {
    await execFileAsync(command, args, { timeout: 3_000 });
    return true;
  } catch (error) {
    const candidate = error as NodeJS.ErrnoException;
    if (candidate.code === "ENOENT") {
      return false;
    }

    return true;
  }
}

export async function checkRuntimeDependencies(): Promise<RuntimeDependencyReport> {
  const report: RuntimeDependencyReport = {
    missingRequired: [],
    missingOptional: []
  };

  for (const dependency of DEPENDENCIES) {
    const available = await isCommandAvailable(dependency.command, dependency.args);
    if (available) {
      continue;
    }

    if (dependency.required) {
      report.missingRequired.push(dependency.label);
    } else {
      report.missingOptional.push(dependency.label);
    }
  }

  return report;
}
