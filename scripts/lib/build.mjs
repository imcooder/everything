import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import { run, runShell } from "./exec.mjs";
import { resolveVersion } from "./version.mjs";

const scriptsDir = path.dirname(fileURLToPath(import.meta.url));
export const repoRoot = path.resolve(scriptsDir, "../..");

export const paths = {
    vcpkgDir: path.join(repoRoot, "vcpkg"),
    vcpkgToolchain: path.join(repoRoot, "vcpkg", "scripts", "buildsystems", "vcpkg.cmake"),
    buildDir: path.join(repoRoot, "build"),
    cmakeCache: path.join(repoRoot, "build", "CMakeCache.txt"),
};

export function requireWindows() {
    if (process.platform !== "win32") {
        console.error("Native build and run require Windows (MSVC + Win32 APIs).");
        process.exit(1);
    }
}

export function ensureVcpkgBinarySources() {
    if (!process.env.VCPKG_BINARY_SOURCES) {
        const cacheDir = path.join(paths.vcpkgDir, "bincache");
        process.env.VCPKG_BINARY_SOURCES = `clear;files,${cacheDir},readwrite`;
    }
}

export function ensureVcpkg() {
    ensureVcpkgBinarySources();

    if (!fs.existsSync(paths.vcpkgToolchain)) {
        if (fs.existsSync(paths.vcpkgDir)) {
            console.log("Incomplete vcpkg directory — recloning...");
            fs.rmSync(paths.vcpkgDir, { recursive: true, force: true });
        }

        console.log("Cloning vcpkg...");
        run("git", [
            "clone",
            "--depth",
            "1",
            "https://github.com/microsoft/vcpkg.git",
            "vcpkg",
        ]);
    }

    const vcpkgExe =
        process.platform === "win32"
            ? path.join(paths.vcpkgDir, "vcpkg.exe")
            : path.join(paths.vcpkgDir, "vcpkg");

    if (!fs.existsSync(vcpkgExe)) {
        console.log("Bootstrapping vcpkg...");
        if (process.platform === "win32") {
            runShell("call vcpkg\\bootstrap-vcpkg.bat -disableMetrics", { cwd: repoRoot });
        } else {
            run(path.join(paths.vcpkgDir, "bootstrap-vcpkg.sh"), ["-disableMetrics"], {
                cwd: paths.vcpkgDir,
            });
        }
    }
}

export function configureCMake() {
    const version = resolveVersion();
    console.log(`Product version: ${version}`);

    console.log("Configuring CMake...");
    run("cmake", [
        "-B",
        "build",
        "-S",
        ".",
        `-DCMAKE_TOOLCHAIN_FILE=${paths.vcpkgToolchain}`,
        `-DEVERYTHING_VERSION=${version}`,
        "-DVCPKG_TARGET_TRIPLET=x64-windows",
        "-A",
        "x64",
    ]);
}

export function isConfigured() {
    return fs.existsSync(paths.cmakeCache);
}

export function exePath(config) {
    return path.join(paths.buildDir, "src", config, "Everything.Core.exe");
}

export function uiExePath(config) {
    return path.join(paths.buildDir, "src", config, "Everything.UI.exe");
}

export function testExePath(config) {
    return path.join(paths.buildDir, "tests", config, "Everything.Tests.exe");
}

export function buildConfig(config) {
    const version = resolveVersion();
    console.log(`Building ${config} (${version})...`);
    run("cmake", ["--build", "build", "--config", config, "--parallel"]);
}

export function verifyBinary(config) {
    const outputExe = exePath(config);
    if (!fs.existsSync(outputExe)) {
        console.error(`Expected output not found: ${outputExe}`);
        process.exit(1);
    }
    console.log(`Built ${outputExe}`);
    return outputExe;
}

export function parseBuildConfig(argv = process.argv) {
    const flagIndex = argv.indexOf("--config");
    if (flagIndex !== -1 && argv[flagIndex + 1]) {
        return argv[flagIndex + 1];
    }

    if (process.env.BUILD_CONFIG) {
        return process.env.BUILD_CONFIG;
    }

    return "Debug";
}
