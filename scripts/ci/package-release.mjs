import fs from "fs";
import path from "path";
import { exePath, repoRoot } from "../lib/build.mjs";
import { resolveVersion } from "../lib/version.mjs";

const version = resolveVersion();
const srcExe = exePath("Release");

if (!fs.existsSync(srcExe)) {
    console.error(`Release binary not found: ${srcExe}`);
    console.error("Run npm run build:release first.");
    process.exit(1);
}

const outDir = path.join(repoRoot, "output", "release");
fs.mkdirSync(outDir, { recursive: true });

const assetName = `Everything.Core-${version}-win-x64.exe`;
const destExe = path.join(outDir, assetName);
fs.copyFileSync(srcExe, destExe);

const manifest = {
    version,
    assetName,
    platform: "win-x64",
    source: path.relative(repoRoot, srcExe),
};
fs.writeFileSync(path.join(outDir, "manifest.json"), `${JSON.stringify(manifest, null, 2)}\n`);

console.log(`Packaged ${destExe}`);
