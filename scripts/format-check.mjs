import fs from "fs";
import path from "path";
import { collectSourceFiles, repoRoot } from "./lib/repo.mjs";
import { requireClangFormat } from "./lib/clang-format.mjs";
import { run, tryRun } from "./lib/exec.mjs";

process.chdir(repoRoot);

const clangFormat = requireClangFormat();
const files = [
    ...collectSourceFiles(path.join(repoRoot, "src")),
    ...collectSourceFiles(path.join(repoRoot, "tests")),
];

if (files.length === 0) {
    console.error("No source files found under src/ or tests/.");
    process.exit(1);
}

console.log(`Using: ${clangFormat}`);
run(clangFormat, ["--version"]);

let failed = false;
for (const file of files) {
    if (!tryRun(clangFormat, ["--dry-run", "--Werror", file])) {
        failed = true;
    }
}

if (failed) {
    console.error("");
    console.error("Formatting check failed. Run: npm run format");
    process.exit(1);
}

console.log(`All ${files.length} file(s) match .clang-format.`);
