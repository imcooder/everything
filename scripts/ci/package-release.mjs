import fs from "fs";
import path from "path";
import { exePath, uiExePath, repoRoot } from "../lib/build.mjs";
import { resolveVersion } from "../lib/version.mjs";
import { runShell } from "../lib/exec.mjs";

const version = resolveVersion();
const srcCoreExe = exePath("Release");
const srcUiExe = uiExePath("Release");

for (const [label, srcExe] of [
    ["console harness (Everything.Core)", srcCoreExe],
    ["UI (Everything.UI)", srcUiExe],
]) {
    if (!fs.existsSync(srcExe)) {
        console.error(`${label} Release binary not found: ${srcExe}`);
        console.error("Run npm run build:release first.");
        process.exit(1);
    }
}

const outDir = path.join(repoRoot, "output", "release");
fs.rmSync(outDir, { recursive: true, force: true });
fs.mkdirSync(outDir, { recursive: true });

const stageDir = path.join(outDir, `Everything-${version}-win-x64`);
fs.mkdirSync(stageDir, { recursive: true });

const coreDestName = "Everything.Core.exe";
const uiDestName = "Everything.exe"; // user-facing name — this is the product, the console harness is a debugging tool
fs.copyFileSync(srcCoreExe, path.join(stageDir, coreDestName));
fs.copyFileSync(srcUiExe, path.join(stageDir, uiDestName));

const readmeText = `Everything (open-source clone) ${version}\n\nEverything.exe        - the search UI (run this)\nEverything.Core.exe   - headless console harness, useful for debugging\n\nBoth read NTFS volumes directly and require Administrator privileges\nfor MFT/USN access. Right-click -> Run as administrator if search\nresults stay empty.\n`;
fs.writeFileSync(path.join(stageDir, "README.txt"), readmeText);

const zipName = `Everything-${version}-win-x64.zip`;
const zipPath = path.join(outDir, zipName);

console.log(`Zipping ${stageDir} -> ${zipPath}`);
runShell(
    `powershell -NoProfile -Command "Compress-Archive -Path '${stageDir}\\*' -DestinationPath '${zipPath}' -Force"`,
);

const manifest = {
    version,
    zipName,
    platform: "win-x64",
    contents: [coreDestName, uiDestName, "README.txt"],
    sources: {
        [coreDestName]: path.relative(repoRoot, srcCoreExe),
        [uiDestName]: path.relative(repoRoot, srcUiExe),
    },
};
fs.writeFileSync(path.join(outDir, "manifest.json"), `${JSON.stringify(manifest, null, 2)}\n`);

console.log(`Packaged ${zipPath}`);
