import fs from "fs";
import path from "path";
import { repoRoot } from "./repo.mjs";

const packagePath = path.join(repoRoot, "package.json");

export function readPackageVersion() {
    const pkg = JSON.parse(fs.readFileSync(packagePath, "utf8"));
    if (!pkg.version || typeof pkg.version !== "string") {
        throw new Error("package.json must define a semver string in \"version\".");
    }
    return pkg.version;
}

export function parseVersionParts(version) {
    const parts = version.split(".");
    if (parts.length < 2) {
        throw new Error(`Invalid version "${version}" — expected at least major.minor.patch.`);
    }

    return {
        major: parts[0],
        minor: parts[1],
        patch: parts[2] ?? "0",
    };
}

/**
 * Release version: major.minor from package.json (edited manually),
 * patch/build from BUILD_NUMBER (GitHub Actions run number in CI).
 */
export function resolveVersion() {
    const { major, minor, patch } = parseVersionParts(readPackageVersion());
    const buildNumber = process.env.BUILD_NUMBER ?? patch;
    return `${major}.${minor}.${buildNumber}`;
}

export function describeVersion() {
    const base = readPackageVersion();
    const resolved = resolveVersion();
    const source = process.env.BUILD_NUMBER ? "BUILD_NUMBER" : "package.json patch";
    return { base, resolved, source };
}
