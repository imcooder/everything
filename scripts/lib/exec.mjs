import { spawnSync } from "child_process";

export function run(command, args, options = {}) {
    const result = spawnSync(command, args, {
        stdio: "inherit",
        shell: false,
        ...options,
    });

    if (result.error) {
        throw result.error;
    }

    if (result.status !== 0) {
        process.exit(result.status ?? 1);
    }

    return result;
}

export function tryRun(command, args, options = {}) {
    const result = spawnSync(command, args, {
        stdio: "inherit",
        shell: false,
        ...options,
    });

    if (result.error) {
        throw result.error;
    }

    return result.status === 0;
}

export function runShell(command, options = {}) {
    const shell = process.platform === "win32";
    const result = spawnSync(command, {
        stdio: "inherit",
        shell,
        ...options,
    });

    if (result.error) {
        throw result.error;
    }

    if (result.status !== 0) {
        process.exit(result.status ?? 1);
    }

    return result;
}
