#!/usr/bin/env node
/**
 * Install repo git hooks (core.hooksPath -> .githooks).
 * Called automatically via npm prepare.
 */
import { spawnSync } from "child_process";
import path from "path";
import { fileURLToPath } from "url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

function git(args) {
    return spawnSync("git", args, { cwd: repoRoot, encoding: "utf8" });
}

const inside = git(["rev-parse", "--is-inside-work-tree"]);
if (inside.status !== 0) {
    process.exit(0);
}

const hooksPath = ".githooks";
const set = git(["config", "core.hooksPath", hooksPath]);
if (set.status !== 0) {
    console.warn("Could not set core.hooksPath — git hooks not installed.");
    process.exit(0);
}

console.log(`Git hooks installed (${hooksPath}/). Pre-push runs format:check.`);
