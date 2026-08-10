import fs from "fs";
import path from "path";
import { uiExePath, repoRoot } from "../lib/build.mjs";
import { resolveVersion } from "../lib/version.mjs";
import { runShell } from "../lib/exec.mjs";

const version = resolveVersion();
const srcUiExe = uiExePath("Release");

// Everything.Core.exe (the headless console harness) stays an internal debugging tool — it is
// not part of the public release. It shares Everything.Backend with Everything.exe, so anything
// verified against Everything.Core.exe covers the same indexing/search/MFT/USN logic end users
// get through the UI; only the presence of a window differs.
if (!fs.existsSync(srcUiExe)) {
    console.error(`UI Release binary not found: ${srcUiExe}`);
    console.error("Run npm run build:release first.");
    process.exit(1);
}

const outDir = path.join(repoRoot, "output", "release");
fs.rmSync(outDir, { recursive: true, force: true });
fs.mkdirSync(outDir, { recursive: true });

const stageDir = path.join(outDir, `Everything-${version}-win-x64`);
fs.mkdirSync(stageDir, { recursive: true });

const uiDestName = "Everything.exe";
fs.copyFileSync(srcUiExe, path.join(stageDir, uiDestName));

const readmeText = `Everything (open-source clone) ${version}\n\nEverything.exe - run this.\n\nReads NTFS volumes directly and requires Administrator privileges for\nMFT/USN access. Right-click -> Run as administrator if search results\nstay empty.\n`;
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
    contents: [uiDestName, "README.txt"],
    sources: {
        [uiDestName]: path.relative(repoRoot, srcUiExe),
    },
};
fs.writeFileSync(path.join(outDir, "manifest.json"), `${JSON.stringify(manifest, null, 2)}\n`);

console.log(`Packaged ${zipPath}`);
