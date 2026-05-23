import fs from "fs";
import { execSync } from "child_process";
import path from "path";

function isExecutable(filePath) {
    try {
        fs.accessSync(filePath, fs.constants.X_OK);
        return true;
    } catch {
        return fs.existsSync(filePath);
    }
}

function resolveOnPath(name) {
    const lookup = process.platform === "win32" ? "where" : "which";
    try {
        const output = execSync(`${lookup} ${name}`, {
            encoding: "utf8",
            stdio: ["ignore", "pipe", "ignore"],
        }).trim();
        const first = output.split(/\r?\n/)[0];
        return first || null;
    } catch {
        return null;
    }
}

export function findClangFormat() {
    if (process.env.CLANG_FORMAT) {
        return process.env.CLANG_FORMAT;
    }

    const candidates = ["clang-format", "clang-format-18", "clang-format-17"];

    if (process.platform === "darwin") {
        candidates.push(
            "/opt/homebrew/opt/llvm/bin/clang-format",
            "/usr/local/opt/llvm/bin/clang-format",
        );
    }

    if (process.platform === "win32") {
        for (const base of [process.env.ProgramFiles, process.env["ProgramFiles(x86)"]]) {
            if (base) {
                candidates.push(path.join(base, "LLVM", "bin", "clang-format.exe"));
            }
        }
    }

    for (const candidate of candidates) {
        if (candidate.includes("/") || candidate.includes("\\")) {
            if (isExecutable(candidate)) {
                return candidate;
            }
            continue;
        }

        const resolved = resolveOnPath(candidate);
        if (resolved) {
            return resolved;
        }
    }

    return null;
}

export function requireClangFormat() {
    const clangFormat = findClangFormat();
    if (!clangFormat) {
        console.error("clang-format not found.");
        console.error("Install LLVM clang-format, then re-run:");
        console.error("  macOS:   brew install clang-format");
        console.error("  Windows: winget install LLVM.LLVM");
        console.error("  Ubuntu:  sudo apt install clang-format-18");
        console.error("Or set CLANG_FORMAT to the executable path.");
        process.exit(1);
    }

    return clangFormat;
}
