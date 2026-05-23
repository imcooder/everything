import { spawnSync } from "child_process";

const branch = process.env.PROTECT_BRANCH ?? "master";

function gh(args, input) {
    const result = spawnSync("gh", args, {
        encoding: "utf8",
        input,
        stdio: ["pipe", "pipe", "pipe"],
    });
    return result;
}

function getRepoSlug() {
    const result = gh(["repo", "view", "--json", "nameWithOwner", "-q", ".nameWithOwner"]);
    if (result.status !== 0) {
        console.error("gh CLI failed. Run: gh auth login");
        console.error(result.stderr?.trim() ?? "");
        process.exit(1);
    }
    return result.stdout.trim();
}

function listExistingChecks(owner, repo, branch) {
    const result = gh([
        "api",
        `repos/${owner}/${repo}/commits/${branch}`,
        "-q",
        ".commit.url",
    ]);
    if (result.status !== 0) {
        return [];
    }

    const shaResult = gh([
        "api",
        `repos/${owner}/${repo}/commits/${branch}`,
        "-q",
        ".sha",
    ]);
    if (shaResult.status !== 0) {
        return [];
    }

    const sha = shaResult.stdout.trim();
    const statusResult = gh([
        "api",
        `repos/${owner}/${repo}/commits/${sha}/status`,
        "-q",
        ".statuses[].context",
    ]);
    if (statusResult.status !== 0) {
        return [];
    }

    return statusResult.stdout
        .split("\n")
        .map((line) => line.trim())
        .filter(Boolean);
}

function resolveCheckContexts(existing) {
    const resolved = [];
    for (const name of ["format", "build", "test"]) {
        if (existing.includes(name)) {
            resolved.push(name);
            continue;
        }
        const prefixed = `CI / ${name}`;
        if (existing.includes(prefixed)) {
            resolved.push(prefixed);
            continue;
        }
        resolved.push(name);
    }
    return [...new Set(resolved)];
}

const slug = getRepoSlug();
const [owner, repo] = slug.split("/");

console.log(`Configuring branch protection for ${slug}:${branch}`);

const existing = listExistingChecks(owner, repo, branch);
const contexts = existing.length > 0 ? resolveCheckContexts(existing) : ["format", "build", "test"];

console.log(`Required status checks: ${contexts.join(", ")}`);
if (existing.length === 0) {
    console.log("Tip: run CI once on GitHub first, then re-run npm run protect:branch to match exact check names.");
}

const payload = {
    required_status_checks: {
        strict: true,
        contexts,
    },
    enforce_admins: true,
    required_pull_request_reviews: {
        dismiss_stale_reviews: false,
        require_code_owner_reviews: false,
        required_approving_review_count: 0,
    },
    restrictions: null,
    required_linear_history: false,
    allow_force_pushes: false,
    allow_deletions: false,
    block_creations: false,
    required_conversation_resolution: true,
    lock_branch: false,
    allow_fork_syncing: true,
};

const result = gh(
    ["api", "-X", "PUT", `repos/${owner}/${repo}/branches/${branch}/protection`, "--input", "-"],
    JSON.stringify(payload),
);

if (result.status !== 0) {
    console.error("Failed to apply branch protection:");
    console.error(result.stderr?.trim() ?? result.stdout?.trim() ?? "");
    console.error("\nRequires repo admin + gh auth login.");
    console.error("Manual setup: GitHub → Settings → Branches → Add rule → require checks: format, build, test");
    process.exit(1);
}

console.log("Branch protection applied.");
console.log("- Pull requests required before merge");
console.log("- Required checks must pass: " + contexts.join(", "));
console.log("- Force push and branch deletion disabled");
