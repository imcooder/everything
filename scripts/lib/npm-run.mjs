import { spawnSync } from "child_process";

export function npmRun(script, extraEnv = {}) {
    const npm = process.platform === "win32" ? "npm.cmd" : "npm";
    const result = spawnSync(npm, ["run", script], {
        stdio: "inherit",
        env: { ...process.env, ...extraEnv },
    });

    if (result.status !== 0) {
        process.exit(result.status ?? 1);
    }
}

export function runCommand(command, args, options = {}) {
    const result = spawnSync(command, args, {
        stdio: "inherit",
        ...options,
    });

    if (result.status !== 0) {
        process.exit(result.status ?? 1);
    }
}
