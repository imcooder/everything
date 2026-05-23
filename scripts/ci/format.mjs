import { npmRun, runCommand } from "../lib/npm-run.mjs";

if (process.platform === "linux" && process.env.CI === "true") {
    console.log("CI: installing clang-format-18...");
    runCommand("sudo", ["apt-get", "update"]);
    runCommand("sudo", ["apt-get", "install", "-y", "clang-format-18"]);
}

npmRun("format:check", {
    CLANG_FORMAT: process.env.CLANG_FORMAT ?? "clang-format-18",
});
