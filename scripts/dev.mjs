import { spawnSync } from "child_process";
import fs from "fs";
import {
    repoRoot,
    requireWindows,
    ensureVcpkg,
    configureCMake,
    isConfigured,
    buildConfig,
    exePath,
} from "./lib/build.mjs";

process.chdir(repoRoot);
requireWindows();

const config = "Debug";
ensureVcpkg();

if (!isConfigured()) {
    console.log("CMake not configured — running setup...");
    configureCMake();
}

const outputExe = exePath(config);
if (!fs.existsSync(outputExe)) {
    buildConfig(config);
}

if (!fs.existsSync(outputExe)) {
    console.error(`Executable not found: ${outputExe}`);
    console.error("Run: npm run build");
    process.exit(1);
}

console.log(`Running ${outputExe}`);
console.log("Run as Administrator for USN volume access.\n");

const result = spawnSync(outputExe, [], {
    stdio: "inherit",
    cwd: repoRoot,
});

if (result.error) {
    throw result.error;
}

process.exit(result.status ?? 0);
