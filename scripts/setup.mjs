import { repoRoot, requireWindows, ensureVcpkg, configureCMake, isConfigured } from "./lib/build.mjs";

process.chdir(repoRoot);
requireWindows();
ensureVcpkg();

if (isConfigured()) {
    console.log("CMake already configured (build/CMakeCache.txt). Skipping configure.");
} else {
    configureCMake();
}

console.log("Setup complete. Run: npm run build  |  npm run build:release  |  npm run dev");
