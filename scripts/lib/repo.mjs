import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

const scriptsDir = path.dirname(fileURLToPath(import.meta.url));

export const repoRoot = path.resolve(scriptsDir, "../..");

export function collectSourceFiles(rootDir = path.join(repoRoot, "src")) {
    const files = [];

    function walk(dir) {
        for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
            const fullPath = path.join(dir, entry.name);
            if (entry.isDirectory()) {
                walk(fullPath);
            } else if (entry.isFile() && (entry.name.endsWith(".cpp") || entry.name.endsWith(".h"))) {
                files.push(fullPath);
            }
        }
    }

    walk(rootDir);
    return files.sort();
}
