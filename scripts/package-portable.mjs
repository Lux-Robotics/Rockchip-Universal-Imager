#!/usr/bin/env node
/**
 * Assemble a portable zip: Tauri app binary + rkdeveloptool + portable marker.
 *
 * Usage (from apps/imager-tauri after `cargo tauri build --no-bundle`):
 *   node ../../scripts/package-portable.mjs
 *
 * Env:
 *   RKDEV_BIN  path to prebuilt rkdeveloptool (required for a complete zip)
 *   OUT_DIR    output directory (default: dist/portable)
 */

import { cpSync, mkdirSync, writeFileSync, existsSync, readdirSync, statSync } from "node:fs";
import { join, dirname, basename } from "node:path";
import { fileURLToPath } from "node:url";
import { execSync } from "node:child_process";

const __dirname = dirname(fileURLToPath(import.meta.url));
const repoRoot = join(__dirname, "..");
const tauriTarget = join(repoRoot, "apps/imager-tauri/src-tauri/target/release");
const outDir = process.env.OUT_DIR || join(repoRoot, "dist/portable");
const isWin = process.platform === "win32";
const appName = isWin ? "rockchip-universal-imager.exe" : "rockchip-universal-imager";
const rkName = isWin ? "rkdeveloptool.exe" : "rkdeveloptool";

function findAppBinary() {
  const direct = join(tauriTarget, appName);
  if (existsSync(direct)) return direct;
  // macOS may produce .app even with --no-bundle in some configs; prefer bare bin
  const candidates = [];
  if (existsSync(tauriTarget)) {
    for (const name of readdirSync(tauriTarget)) {
      const p = join(tauriTarget, name);
      if (name === appName || name.startsWith("rockchip-universal-imager")) {
        candidates.push(p);
      }
    }
  }
  return candidates[0] || null;
}

mkdirSync(outDir, { recursive: true });
const appBin = findAppBinary();
if (!appBin) {
  console.error(`App binary not found under ${tauriTarget}. Run: cargo tauri build --no-bundle`);
  process.exit(1);
}

const staging = join(outDir, "rockchip-universal-imager-portable");
mkdirSync(staging, { recursive: true });
cpSync(appBin, join(staging, basename(appBin)));

const rkdev = process.env.RKDEV_BIN;
if (rkdev && existsSync(rkdev)) {
  cpSync(rkdev, join(staging, rkName));
  console.log(`staged rkdeveloptool from ${rkdev}`);
} else {
  console.warn(
    `RKDEV_BIN not set or missing — zip will lack ${rkName}. Build C++ rkdeveloptool separately and re-run.`
  );
}

writeFileSync(join(staging, "portable"), "");
const loaders = join(repoRoot, "loader_binaries");
if (existsSync(loaders)) {
  cpSync(loaders, join(staging, "loader_binaries"), { recursive: true });
}

const zipPath = join(outDir, `rockchip-universal-imager-${process.platform}-${process.arch}.zip`);
try {
  if (isWin) {
    execSync(
      `powershell -Command "Compress-Archive -Path '${staging}\\*' -DestinationPath '${zipPath}' -Force"`,
      { stdio: "inherit" }
    );
  } else {
    execSync(`cd "${staging}" && zip -r "${zipPath}" .`, { stdio: "inherit" });
  }
  console.log(`portable zip: ${zipPath}`);
} catch (e) {
  console.error("zip failed; staging folder left at", staging);
  process.exit(1);
}
