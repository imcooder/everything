import { spawnSync } from "child_process";

export function npmRun(script, extraEnv = {}) {
    const isWindows = process.platform === "win32";
    // Node 20+ on Windows rejects spawning .cmd without shell (CVE-2024-27980).
    const result = spawnSync(isWindows ? "npm.cmd" : "npm", ["run", script], {
        stdio: "inherit",
        env: { ...process.env, ...extraEnv },
        ...(isWindows ? { shell: true } : {}),
    });

    if (result.error) {
        console.error(result.error.message);
        process.exit(1);
    }

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
