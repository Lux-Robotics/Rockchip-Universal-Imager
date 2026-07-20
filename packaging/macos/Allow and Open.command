#!/bin/bash
# Double-click this file after installing Rockchip Universal Imager.
#
# Apple Gatekeeper blocks apps that are not notarized by Apple. This build is
# free/unsigned (ad-hoc). Running this script removes the download quarantine
# flag so the .app can launch, then opens it.
#
# You may still need to approve once:
#   System Settings → Privacy & Security → "Open Anyway"
# or right-click the app → Open → Open.

set -euo pipefail

APP_NAME="Rockchip Universal Imager.app"
PRODUCT="Rockchip Universal Imager"

# Directory containing this .command (works when double-clicked from Finder)
HERE="$(cd "$(dirname "$0")" && pwd)"

pick_app() {
  local candidates=(
    "/Applications/${APP_NAME}"
    "${HOME}/Applications/${APP_NAME}"
    "${HERE}/${APP_NAME}"
  )
  local c
  for c in "${candidates[@]}"; do
    if [[ -d "$c" ]]; then
      printf '%s' "$c"
      return 0
    fi
  done
  return 1
}

echo
echo "=== ${PRODUCT} — allow & open ==="
echo

APP="$(pick_app || true)"
if [[ -z "${APP:-}" ]]; then
  echo "Could not find ${APP_NAME}."
  echo
  echo "Install first:"
  echo "  1. Drag \"${APP_NAME}\" into Applications"
  echo "  2. Run this script again (from the disk image or from Applications"
  echo "     if you copied it there)."
  echo
  echo "Or open Terminal and run:"
  echo "  xattr -dr com.apple.quarantine \"/Applications/${APP_NAME}\""
  echo "  open \"/Applications/${APP_NAME}\""
  echo
  read -r -p "Press Return to close…" _
  exit 1
fi

echo "App: ${APP}"
echo

# 1) Clear quarantine (main Gatekeeper download block)
if xattr -dr com.apple.quarantine "$APP" 2>/dev/null; then
  echo "Removed quarantine attributes."
else
  echo "No quarantine attributes (or already cleared)."
fi

# 2) Ensure main binary is executable (zip/DMG can drop +x)
MAIN="$APP/Contents/MacOS/rockchip-universal-imager"
if [[ -f "$MAIN" ]]; then
  chmod +x "$MAIN" 2>/dev/null || true
  # Companion tool embedded next to it
  if [[ -f "$APP/Contents/MacOS/rkdeveloptool" ]]; then
    chmod +x "$APP/Contents/MacOS/rkdeveloptool" 2>/dev/null || true
  fi
  echo "Checked execute permissions."
fi

# 3) Ad-hoc re-sign if codesign is available (helps after attribute/chmod changes)
if command -v codesign >/dev/null 2>&1; then
  if codesign --force --deep --sign - "$APP" 2>/dev/null; then
    echo "Ad-hoc code signature refreshed."
  else
    echo "codesign skipped (not required if the app already launches)."
  fi
fi

echo
echo "Opening ${PRODUCT}…"
open "$APP" || {
  echo
  echo "open failed. Try:"
  echo "  • Right-click ${APP_NAME} → Open → Open"
  echo "  • System Settings → Privacy & Security → Open Anyway"
  echo
  read -r -p "Press Return to close…" _
  exit 1
}

echo
echo "Done. If macOS still blocks the app, use Privacy & Security → Open Anyway."
echo
# Brief pause so a double-clicked Terminal window is readable
sleep 2
