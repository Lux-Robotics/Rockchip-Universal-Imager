#!/usr/bin/env bash
# Build a styled macOS installer DMG.
#
# Layout on the volume:
#   Rockchip Universal Imager/     ← single folder to drag into Applications
#     Rockchip Universal Imager.app
#     rkdeveloptool
#     loader_binaries/
#   Applications -> /Applications
#   (.background/ with arrow + instruction art)
#
# Usage:
#   packaging/macos/make-dmg.sh <stage_dir> <out.dmg> [volume_name]
#
# stage_dir must contain:
#   Rockchip Universal Imager.app
#   rkdeveloptool
#   loader_binaries/
#
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "make-dmg.sh must run on macOS (hdiutil/osascript)." >&2
  exit 1
fi

STAGE="${1:?stage_dir required}"
OUT_DMG="${2:?out.dmg required}"
PRODUCT_NAME="${3:-Rockchip Universal Imager}"
APP_BUNDLE="${PRODUCT_NAME}.app"
INSTALL_FOLDER="${PRODUCT_NAME}"
RK_NAME="${RK_NAME:-rkdeveloptool}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BG_SRC="${DMG_BACKGROUND:-$SCRIPT_DIR/dmg-background.png}"

if [[ ! -d "$STAGE/$APP_BUNDLE" ]]; then
  echo "ERROR: missing $STAGE/$APP_BUNDLE" >&2
  exit 1
fi
if [[ ! -f "$STAGE/$RK_NAME" ]]; then
  echo "ERROR: missing $STAGE/$RK_NAME" >&2
  exit 1
fi
if [[ ! -d "$STAGE/loader_binaries" ]]; then
  echo "ERROR: missing $STAGE/loader_binaries" >&2
  exit 1
fi
if [[ ! -f "$BG_SRC" ]]; then
  echo "ERROR: missing DMG background $BG_SRC" >&2
  echo "  Generate with: swift packaging/macos/generate-dmg-background.swift" >&2
  exit 1
fi

WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/rui-dmg.XXXXXX")"
RW_DMG="${WORKDIR}/rw.dmg"
CONTENT="${WORKDIR}/content"
MOUNT_ROOT="${WORKDIR}/mnt"
mkdir -p "$CONTENT/$INSTALL_FOLDER" "$MOUNT_ROOT"

cleanup() {
  # Detach any volume still mounted from this run
  if [[ -n "${DEVICE:-}" ]]; then
    hdiutil detach "$DEVICE" -force >/dev/null 2>&1 || true
  fi
  # Also try by mount point name
  if [[ -d "/Volumes/${PRODUCT_NAME}" ]]; then
    hdiutil detach "/Volumes/${PRODUCT_NAME}" -force >/dev/null 2>&1 || true
  fi
  rm -rf "$WORKDIR"
}
trap cleanup EXIT

echo "==> DMG content: single install folder + Applications link"
cp -R "$STAGE/$APP_BUNDLE" "$CONTENT/$INSTALL_FOLDER/"
cp "$STAGE/$RK_NAME" "$CONTENT/$INSTALL_FOLDER/"
chmod +x "$CONTENT/$INSTALL_FOLDER/$RK_NAME" 2>/dev/null || true
cp -R "$STAGE/loader_binaries" "$CONTENT/$INSTALL_FOLDER/"
# Ensure app binary stays executable after copies
if [[ -d "$CONTENT/$INSTALL_FOLDER/$APP_BUNDLE/Contents/MacOS" ]]; then
  find "$CONTENT/$INSTALL_FOLDER/$APP_BUNDLE/Contents/MacOS" -type f -exec chmod u+x {} + 2>/dev/null || true
fi
ln -sf /Applications "$CONTENT/Applications"

# Size estimate: content + headroom for .DS_Store / background
CONTENT_KB=$(du -sk "$CONTENT" | awk '{print $1}')
# hdiutil wants sectors or a size like 100m; add 20MB padding, min 50MB
SIZE_MB=$(( (CONTENT_KB / 1024) + 20 ))
if [[ "$SIZE_MB" -lt 50 ]]; then
  SIZE_MB=50
fi

echo "==> Create read-write DMG (${SIZE_MB}m)"
rm -f "$RW_DMG"
hdiutil create \
  -volname "$PRODUCT_NAME" \
  -srcfolder "$CONTENT" \
  -fs HFS+ \
  -fsargs "-c c=64,a=16,e=16" \
  -format UDRW \
  -size "${SIZE_MB}m" \
  -ov \
  "$RW_DMG" >/dev/null

echo "==> Mount read-write volume"
# Attach without auto-open; parse device node (e.g. /dev/disk4)
ATTACH_OUT="$(hdiutil attach -readwrite -noverify -noautoopen "$RW_DMG")"
echo "$ATTACH_OUT"
DEVICE="$(echo "$ATTACH_OUT" | awk 'NR==1 {print $1}')"
VOLUME="/Volumes/${PRODUCT_NAME}"

# Wait for mount
for _ in $(seq 1 50); do
  if [[ -d "$VOLUME" ]]; then
    break
  fi
  sleep 0.1
done
if [[ ! -d "$VOLUME" ]]; then
  echo "ERROR: volume not mounted at $VOLUME" >&2
  exit 1
fi

echo "==> Install background + Finder window layout"
mkdir -p "$VOLUME/.background"
cp "$BG_SRC" "$VOLUME/.background/background.png"
# Keep support files out of the icon grid when possible
if command -v SetFile >/dev/null 2>&1; then
  SetFile -a V "$VOLUME/.background" 2>/dev/null || true
fi
if command -v chflags >/dev/null 2>&1; then
  chflags hidden "$VOLUME/.background" 2>/dev/null || true
fi

# Bless so the volume opens to this folder in Finder
if command -v bless >/dev/null 2>&1; then
  bless --folder "$VOLUME" --openfolder "$VOLUME" 2>/dev/null || true
fi

# Position icons to align with drop zones on the background (660×400 window).
# Finder icon positions use top-left origin of the content area.
# Left zone center ~ (160, 200), right ~ (500, 200) in 660×400 design coords.
# Window styling needs a GUI session; content layout still ships if this fails.
if ! osascript <<EOF
tell application "Finder"
  tell disk "$PRODUCT_NAME"
    open
    set current view of container window to icon view
    set toolbar visible of container window to false
    set statusbar visible of container window to false
    set the bounds of container window to {200, 120, 860, 520}
    set theViewOptions to the icon view options of container window
    set arrangement of theViewOptions to not arranged
    set icon size of theViewOptions to 96
    set text size of theViewOptions to 12
    set background picture of theViewOptions to file ".background:background.png"
    -- Icon positions (x, y from top-left of window content)
    set position of item "$INSTALL_FOLDER" of container window to {160, 200}
    set position of item "Applications" of container window to {500, 200}
    update without registering applications
    delay 1
    close
    open
    delay 1
    close
  end tell
end tell
EOF
then
  echo "WARNING: Finder window styling failed (no GUI session?)." >&2
  echo "  DMG still contains the install folder + Applications link." >&2
fi

# Ensure .DS_Store is flushed
sync
sleep 1

echo "==> Detach and compress to UDZO"
hdiutil detach "$DEVICE" -force >/dev/null
DEVICE=""

mkdir -p "$(dirname "$OUT_DMG")"
rm -f "$OUT_DMG"
hdiutil convert "$RW_DMG" -format UDZO -imagekey zlib-level=9 -o "$OUT_DMG" >/dev/null

test -f "$OUT_DMG"
echo "  installer -> $OUT_DMG"
ls -la "$OUT_DMG"
