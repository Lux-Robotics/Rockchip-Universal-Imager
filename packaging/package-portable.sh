#!/usr/bin/env bash
# Assemble a portable zip: Tauri app binary + rkdeveloptool + portable marker.
#
# Usage (after `cargo tauri build --no-bundle` in apps/imager-tauri):
#   ./packaging/package-portable.sh
#   RKDEV_BIN=/path/to/rkdeveloptool ./packaging/package-portable.sh
#
# Env:
#   RKDEV_BIN  path to prebuilt rkdeveloptool (needed for a complete zip)
#   OUT_DIR    output directory (default: dist/portable)
#
# Requires: bash, zip (macOS/Linux). On Windows use Git Bash or WSL.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TAURI_RELEASE="$ROOT/apps/imager-tauri/src-tauri/target/release"
OUT_DIR="${OUT_DIR:-$ROOT/dist/portable}"
STAGING="$OUT_DIR/rockchip-universal-imager-portable"

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*|Windows_NT)
    APP_NAME="rockchip-universal-imager.exe"
    RK_NAME="rkdeveloptool.exe"
    ;;
  *)
    APP_NAME="rockchip-universal-imager"
    RK_NAME="rkdeveloptool"
    ;;
esac

ARCH="$(uname -m)"
# uname -s: Darwin, Linux, MINGW64_NT-..., etc. → short label for zip name
OS_LABEL="$(uname -s | tr '[:upper:]' '[:lower:]' | sed 's/_.*//')"
ZIP_PATH="$OUT_DIR/rockchip-universal-imager-${OS_LABEL}-${ARCH}.zip"

find_app_binary() {
  local direct="$TAURI_RELEASE/$APP_NAME"
  if [[ -f "$direct" ]]; then
    echo "$direct"
    return 0
  fi
  # Fallback: any matching name under release/
  if [[ -d "$TAURI_RELEASE" ]]; then
    local candidate
    candidate="$(find "$TAURI_RELEASE" -maxdepth 1 -type f -name 'rockchip-universal-imager*' 2>/dev/null | head -1 || true)"
    if [[ -n "${candidate:-}" && -f "$candidate" ]]; then
      echo "$candidate"
      return 0
    fi
  fi
  return 1
}

if ! command -v zip >/dev/null 2>&1; then
  echo "error: 'zip' not found (install zip, or use Git Bash with zip on Windows)" >&2
  exit 1
fi

APP_BIN="$(find_app_binary)" || {
  echo "error: app binary not found under $TAURI_RELEASE" >&2
  echo "  run: cargo tauri build --manifest-path apps/imager-tauri/src-tauri/Cargo.toml --no-bundle" >&2
  exit 1
}

rm -rf "$STAGING"
mkdir -p "$STAGING"
cp "$APP_BIN" "$STAGING/$(basename "$APP_BIN")"

if [[ -n "${RKDEV_BIN:-}" && -f "$RKDEV_BIN" ]]; then
  cp "$RKDEV_BIN" "$STAGING/$RK_NAME"
  chmod +x "$STAGING/$RK_NAME" 2>/dev/null || true
  echo "staged rkdeveloptool from $RKDEV_BIN"
else
  echo "warning: RKDEV_BIN not set or missing — zip will lack $RK_NAME" >&2
  echo "  build C++ rkdeveloptool separately and re-run with RKDEV_BIN=..." >&2
fi

# Empty marker: app logs next to the extract folder instead of OS log dirs
: >"$STAGING/portable"

if [[ -d "$ROOT/loader_binaries" ]]; then
  cp -R "$ROOT/loader_binaries" "$STAGING/loader_binaries"
fi

# Ensure the GUI binary is executable on Unix
chmod +x "$STAGING/$(basename "$APP_BIN")" 2>/dev/null || true

mkdir -p "$OUT_DIR"
rm -f "$ZIP_PATH"
(
  cd "$STAGING"
  zip -r "$ZIP_PATH" .
)

echo "portable zip: $ZIP_PATH"
