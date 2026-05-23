import { readFileSync } from "fs";
import path from "path";
import { fileURLToPath } from "url";

const scriptsDir = path.dirname(fileURLToPath(import.meta.url));
const pkg = JSON.parse(readFileSync(path.join(scriptsDir, "../../package.json"), "utf8"));

function required(name) {
    const value = process.env[name];
    if (!value) {
        console.error(`Missing environment variable: ${name}`);
        process.exit(1);
    }
    return value;
}

function optional(name, fallback = "") {
    return process.env[name] ?? fallback;
}

const phase = (() => {
    const index = process.argv.indexOf("--phase");
    if (index === -1 || !process.argv[index + 1]) {
        console.error("Usage: node scripts/ci/release-notes.mjs --phase initial|final");
        process.exit(1);
    }
    return process.argv[index + 1];
})();

const version = required("VERSION");
const releaseTag = required("RELEASE_TAG");
const repo = required("GITHUB_REPOSITORY");
const branch = required("BRANCH");
const sha = required("GITHUB_SHA");
const runId = required("GITHUB_RUN_ID");
const buildTime = required("BUILD_TIME");
const workflowUrl = `https://github.com/${repo}/actions/runs/${runId}`;
const releaseUrl = `https://github.com/${repo}/releases/tag/${releaseTag}`;
const assetName = `Everything.Core-${version}-win-x64.exe`;
const shortSha = sha.slice(0, 7);

const header = `# Everything.Core v${version}

> High-performance NTFS file search — independent reimplementation inspired by [voidtools Everything](https://www.voidtools.com/).

## Release Information

| Field | Value |
|-------|-------|
| **Version** | \`${version}\` |
| **Tag** | [\`${releaseTag}\`](${releaseUrl}) |
| **Package base** | \`${pkg.version}\` (major.minor from \`package.json\`) |
| **Build number** | \`${optional("BUILD_NUMBER", version.split(".").pop())}\` |
| **Release date** | ${buildTime} |
| **Branch** | \`${branch}\` |
| **Commit** | [\`${shortSha}\`](https://github.com/${repo}/commit/${sha}) |
| **Workflow** | [Run #${runId}](${workflowUrl}) |

---
`;

if (phase === "initial") {
    process.stdout.write(`${header}
## Status

> **Build in progress…** CI (format, build, test) is running. Download links will appear below once packaging finishes.

## Planned assets

| Platform | File |
|----------|------|
| Windows x64 | \`${assetName}\` |

## Requirements

- Windows 10 / 11 (x64)
- NTFS volumes
- Run **as Administrator** (MFT / USN access)

## Installation

1. Download \`${assetName}\` from the **Assets** section (available after the workflow completes).
2. Place the executable anywhere on disk (portable) or add to \`PATH\`.
3. Launch from an elevated command prompt or terminal.

## Notes

- This is a **console prototype** (\`Everything.Core.exe\`) — UI milestone is planned separately.
- Search targets NTFS fixed disks via USN journal indexing; see the [README](https://github.com/${repo}#readme) for architecture details.
`);
    process.exit(0);
}

if (phase === "final") {
    const buildResult = optional("BUILD_RESULT", "success");
    const testResult = optional("TEST_RESULT", "success");
    const formatResult = optional("FORMAT_RESULT", "success");

    process.stdout.write(`${header}
## Build Results

| Stage | Result |
|-------|--------|
| Format | ${formatResult} |
| Build (Release) | ${buildResult} |
| Test | ${testResult} |

---

## Download

### Windows (x64)

| File | Description |
|------|-------------|
| [\`${assetName}\`](${releaseUrl}) | Release build (\`Everything.Core.exe\`, renamed for distribution) |

> **Direct download:** open the [release page](${releaseUrl}), expand **Assets**, and download \`${assetName}\`.

---

## Requirements

- Windows 10 / 11 (x64)
- NTFS volumes
- Run **as Administrator** (MFT / USN access)

## Installation

1. Download \`${assetName}\` from the Assets section above.
2. Optionally verify the workflow run and commit match the build you expect.
3. Run from an **elevated** terminal:

\`\`\`powershell
.\\${assetName}
\`\`\`

## What's in this build

- Per-volume USN index on a dedicated I/O thread
- Streaming search with cancellation (\`ISearchSink\`)
- Unit tests (GoogleTest) passed in CI before packaging

## Links

- [Repository](https://github.com/${repo})
- [Release tag \`${releaseTag}\`](${releaseUrl})
- [Source at \`${shortSha}\`](https://github.com/${repo}/tree/${sha})
`);
    process.exit(0);
}

console.error(`Unknown phase "${phase}" — use initial or final.`);
process.exit(1);
