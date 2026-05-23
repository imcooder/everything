import {
    repoRoot,
    requireWindows,
    ensureVcpkg,
    configureCMake,
    isConfigured,
    buildConfig,
    verifyBinary,
    parseBuildConfig,
} from "./lib/build.mjs";

process.chdir(repoRoot);
requireWindows();

const config = parseBuildConfig();
ensureVcpkg();

if (!isConfigured()) {
    console.log("CMake not configured — running setup...");
    configureCMake();
}

buildConfig(config);
verifyBinary(config);
