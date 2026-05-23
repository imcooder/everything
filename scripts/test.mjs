import fs from "fs";
import {
    repoRoot,
    requireWindows,
    ensureVcpkg,
    configureCMake,
    isConfigured,
    buildConfig,
    testExePath,
    parseBuildConfig,
} from "./lib/build.mjs";
import { run } from "./lib/exec.mjs";

process.chdir(repoRoot);
requireWindows();

const config = parseBuildConfig();

ensureVcpkg();

if (!isConfigured()) {
    console.log("CMake not configured — running setup...");
    configureCMake();
}

console.log(`Building tests (${config})...`);
run("cmake", [
    "--build",
    "build",
    "--config",
    config,
    "--target",
    "Everything.Tests",
    "--parallel",
]);

const testExe = testExePath(config);
if (!fs.existsSync(testExe)) {
    console.error(`Test executable not found: ${testExe}`);
    process.exit(1);
}

console.log(`Running ${testExe}`);
run("ctest", ["--test-dir", "build", "-C", config, "--output-on-failure"]);
