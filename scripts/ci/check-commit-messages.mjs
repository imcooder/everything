#!/usr/bin/env node
// CI commit-message gate: every commit introduced by this push/PR must be English-only, carry no
// AI-model/vendor mentions, no "Co-Authored-By" trailer, and no email outside the allowlist (see
// scripts/lib/commit-lint.mjs — the exact same rules the local commit-msg hook enforces).
import { execFileSync } from "child_process";
import { checkMessage } from "../lib/commit-lint.mjs";

function resolveCommitRange() {
    // The workflow sets COMMIT_LINT_BASE explicitly for both push and pull_request events (see
    // .github/workflows/ci.yml) — a plain SHA/ref to diff HEAD against, already accounting for
    // the "first push to a new branch" (all-zero before-SHA) edge case there.
    const base = process.env.COMMIT_LINT_BASE;
    return base ? `${base}..HEAD` : "HEAD~1..HEAD";
}

function listCommits(range) {
    const output = execFileSync("git", ["log", range, "--format=%H"], { encoding: "utf8" }).trim();
    return output.length > 0 ? output.split("\n") : [];
}

function getCommitMessage(sha) {
    return execFileSync("git", ["log", "-1", "--format=%B", sha], { encoding: "utf8" });
}

const range = resolveCommitRange();
console.log(`Checking commit messages in range: ${range}`);

const shas = listCommits(range);
if (shas.length === 0) {
    console.log("No new commits to check.");
    process.exit(0);
}

let failed = false;

for (const sha of shas) {
    const message = getCommitMessage(sha);
    const problems = checkMessage(message);

    if (problems.length > 0) {
        failed = true;
        console.error(`\ncommit ${sha}:`);
        for (const problem of problems) {
            console.error(`  - ${problem}`);
        }
    }
}

if (failed) {
    console.error("\nCommit message check failed. Rewrite the offending commit message(s) and force-push.");
    process.exit(1);
}

console.log(`${shas.length} commit(s) OK.`);
