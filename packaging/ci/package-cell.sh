#!/usr/bin/env bash
# Assemble one OS/arch portable archive + real installer from prebuilt artifacts.
#
# Portable (no markers/README): zip of
#   Windows/Linux: app binary + rkdeveloptool + loader_binaries/
#   macOS:         Rockchip Universal Imager.app + rkdeveloptool + loader_binaries/
#
# Installer:
#   Windows: NSIS .exe  → Program Files\Rockchip Universal Imager\
#   macOS:   DMG        → drag "Rockchip Universal Imager" folder into /Applications
#                         (folder holds .app + rkdeveloptool + loader_binaries/)
#   Linux:   .deb       → /opt/rockchip-universal-imager/
#
# Logs always use OS user log dirs (not the install/portable folder).
#
# Env:
#   OS_LABEL ARCH_LABEL APP_NAME RK_NAME
#   IN_ROOT (default dist/in) OUT_ROOT (default dist/out)
#   GITHUB_WORKSPACE / repo root with packaging/ and loader_binaries/

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

OS_LABEL="${OS_LABEL:?}"
ARCH_LABEL="${ARCH_LABEL:?}"
APP_NAME="${APP_NAME:?}"
RK_NAME="${RK_NAME:?}"
IN_ROOT="${IN_ROOT:-$ROOT/dist/in}"
OUT_ROOT="${OUT_ROOT:-$ROOT/dist/out}"
LOADER_SRC="${LOADER_SRC:-$ROOT/loader_binaries}"
PRODUCT_NAME="Rockchip Universal Imager"
APP_BUNDLE="${PRODUCT_NAME}.app"

app_dir="${IN_ROOT}/app-${OS_LABEL}-${ARCH_LABEL}"
rk_dir="${IN_ROOT}/rkdeveloptool-${OS_LABEL}-${ARCH_LABEL}"

echo "==> package-cell ${OS_LABEL}-${ARCH_LABEL}"
echo "  app_dir=$app_dir"
echo "  rk_dir=$rk_dir"

if [[ ! -d "$app_dir" ]]; then
  echo "ERROR: missing app artifact $app_dir" >&2
  exit 1
fi
if [[ ! -d "$rk_dir" ]]; then
  echo "ERROR: missing rkdeveloptool artifact $rk_dir" >&2
  exit 1
fi

find_one_file() {
  local dir="$1"
  shift
  local n c
  for n in "$@"; do
    if [[ -f "$dir/$n" ]]; then
      echo "$dir/$n"
      return 0
    fi
    c="$(find "$dir" -maxdepth 3 -type f -name "$n" 2>/dev/null | head -1 || true)"
    if [[ -n "$c" && -f "$c" ]]; then
      echo "$c"
      return 0
    fi
  done
  return 1
}

find_app_bundle() {
  local dir="$1"
  if [[ -d "$dir/$APP_BUNDLE" ]]; then
    echo "$dir/$APP_BUNDLE"
    return 0
  fi
  local c
  c="$(find "$dir" -maxdepth 3 -type d -name "*.app" 2>/dev/null | head -1 || true)"
  if [[ -n "$c" && -d "$c" ]]; then
    echo "$c"
    return 0
  fi
  return 1
}

rk_bin="$(find_one_file "$rk_dir" "$RK_NAME" "rkdeveloptool.exe" "rkdeveloptool")"
test -f "$rk_bin"

stage="${OUT_ROOT}/staging/${OS_LABEL}-${ARCH_LABEL}"
rm -rf "$stage"
mkdir -p "$stage/loader_binaries"

if [[ "$OS_LABEL" == "macos" ]]; then
  app_src="$(find_app_bundle "$app_dir")"
  test -d "$app_src"
  cp -R "$app_src" "$stage/"
  # Ensure bundle name is stable
  if [[ "$(basename "$app_src")" != "$APP_BUNDLE" ]]; then
    mv "$stage/$(basename "$app_src")" "$stage/$APP_BUNDLE"
  fi
  # GitHub Actions artifacts strip Unix execute bits. Without +x, LaunchServices
  # fails with "The application can't be opened" / launchd spawn error 111.
  app_macos="$stage/$APP_BUNDLE/Contents/MacOS"
  if [[ -d "$app_macos" ]]; then
    # Main binary + any bundled helpers/dylibs that need to load
    find "$app_macos" -type f -exec chmod u+x {} +
    # Mark Mach-O executables explicitly (idempotent)
    if [[ -f "$app_macos/$APP_NAME" ]]; then
      chmod +x "$app_macos/$APP_NAME"
    fi
  fi
  # Re-sign after permission / copy mutations. Ad-hoc is enough for unsigned dist;
  # linker-only signatures leave Info.plist unbound and break some open paths.
  if command -v codesign >/dev/null 2>&1; then
    codesign --force --deep --sign - "$stage/$APP_BUNDLE"
    codesign --verify --verbose=2 "$stage/$APP_BUNDLE" 2>&1 || true
  fi
  # Sanity: refuse to ship a non-executable main binary
  main_bin="$app_macos/$APP_NAME"
  if [[ -f "$main_bin" && ! -x "$main_bin" ]]; then
    echo "ERROR: $main_bin is not executable after packaging" >&2
    ls -la "$app_macos" >&2
    exit 1
  fi
else
  app_bin="$(find_one_file "$app_dir" "$APP_NAME" "rockchip-universal-imager.exe" "rockchip-universal-imager")"
  test -f "$app_bin"
  cp "$app_bin" "$stage/$APP_NAME"
  chmod +x "$stage/$APP_NAME" 2>/dev/null || true
fi

cp "$rk_bin" "$stage/$RK_NAME"
chmod +x "$stage/$RK_NAME" 2>/dev/null || true

if [[ -d "$LOADER_SRC" ]]; then
  cp -R "$LOADER_SRC"/. "$stage/loader_binaries/" 2>/dev/null || true
fi

# No portable marker, no README — just the apps + loaders.
mkdir -p "$OUT_ROOT/portable" "$OUT_ROOT/installer"

port_name="rockchip-universal-imager-portable-${OS_LABEL}-${ARCH_LABEL}"
port_zip="${OUT_ROOT}/portable/${port_name}.zip"
(
  cd "$stage/.."
  rm -f "$port_zip"
  # zip contents are the files inside stage (not an extra wrapper folder name mismatch)
  # Use folder name = port_name for a clean extract
  rm -rf "${OUT_ROOT}/portable/_tree/${port_name}"
  mkdir -p "${OUT_ROOT}/portable/_tree"
  cp -R "$stage" "${OUT_ROOT}/portable/_tree/${port_name}"
  (
    cd "${OUT_ROOT}/portable/_tree"
    zip -r "$port_zip" "$port_name"
  )
)
echo "  portable -> $port_zip"
ls -la "$port_zip"

# --- Real installers (must run on matching OS for NSIS / hdiutil / dpkg) ---
case "$OS_LABEL" in
  windows)
    out_exe="${OUT_ROOT}/installer/rockchip-universal-imager-${OS_LABEL}-${ARCH_LABEL}-setup.exe"
    mkdir -p "$(dirname "$out_exe")"
    nsi="$ROOT/packaging/windows/installer.nsi"
    test -f "$nsi"

    # winget installs NSIS under Program Files (x86); may not be on PATH
    export PATH="/c/Program Files (x86)/NSIS:/c/Program Files/NSIS:$PATH"
    if ! command -v makensis >/dev/null 2>&1; then
      for c in \
        "/c/Program Files (x86)/NSIS/makensis.exe" \
        "/c/Program Files/NSIS/makensis.exe"; do
        if [[ -f "$c" ]]; then
          makensis() { "$c" "$@"; }
          break
        fi
      done
    fi
    if ! command -v makensis >/dev/null 2>&1 && ! type makensis >/dev/null 2>&1; then
      echo "ERROR: makensis not found. Install NSIS on the Windows runner:" >&2
      echo "  winget install NSIS.NSIS" >&2
      exit 1
    fi

    stage_win="$stage"
    out_win="$out_exe"
    nsi_win="$nsi"
    if command -v cygpath >/dev/null 2>&1; then
      stage_win="$(cygpath -w "$stage")"
      out_win="$(cygpath -w "$out_exe")"
      nsi_win="$(cygpath -w "$nsi")"
    fi
    makensis -V2 \
      "-DSTAGE_DIR=$stage_win" \
      "-DOUT_EXE=$out_win" \
      "$nsi_win"
    test -f "$out_exe"
    echo "  installer -> $out_exe"
    ;;

  macos)
    out_dmg="${OUT_ROOT}/installer/rockchip-universal-imager-${OS_LABEL}-${ARCH_LABEL}.dmg"
    mkdir -p "$(dirname "$out_dmg")"
    # Styled DMG: one install folder (app + companions) + Applications shortcut,
    # arrow background, and drag instructions (see packaging/macos/make-dmg.sh).
    make_dmg="$ROOT/packaging/macos/make-dmg.sh"
    test -f "$make_dmg"
    chmod +x "$make_dmg"
    RK_NAME="$RK_NAME" "$make_dmg" "$stage" "$out_dmg" "$PRODUCT_NAME"
    test -f "$out_dmg"
    ;;

  linux)
    out_deb="${OUT_ROOT}/installer/rockchip-universal-imager-${OS_LABEL}-${ARCH_LABEL}.deb"
    mkdir -p "$(dirname "$out_deb")"
    deb_root="${OUT_ROOT}/installer/_deb-${OS_LABEL}-${ARCH_LABEL}"
    rm -rf "$deb_root"
    install_root="$deb_root/opt/rockchip-universal-imager"
    mkdir -p "$install_root" "$deb_root/DEBIAN" \
      "$deb_root/usr/share/applications" \
      "$deb_root/usr/bin"

    cp "$stage/$APP_NAME" "$install_root/"
    cp "$stage/$RK_NAME" "$install_root/"
    cp -R "$stage/loader_binaries" "$install_root/"
    chmod 755 "$install_root/$APP_NAME" "$install_root/$RK_NAME"

    # PATH shim
    ln -sf /opt/rockchip-universal-imager/"$APP_NAME" \
      "$deb_root/usr/bin/rockchip-universal-imager"

    cat >"$deb_root/usr/share/applications/rockchip-universal-imager.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Rockchip Universal Imager
Comment=Rockchip flashing and eMMC helper
Exec=/opt/rockchip-universal-imager/${APP_NAME}
Terminal=false
Categories=Utility;Development;
EOF

    ver="${VERSION:-0.1.0}"
    arch_deb="amd64"
    [[ "$ARCH_LABEL" == "aarch64" ]] && arch_deb="arm64"
    cat >"$deb_root/DEBIAN/control" <<EOF
Package: rockchip-universal-imager
Version: ${ver}
Section: utils
Priority: optional
Architecture: ${arch_deb}
Maintainer: Rockchip Universal Imager
Description: Rockchip flashing and eMMC helper
 Cross-platform Rockchip USB flashing utility (Tauri GUI + rkdeveloptool).
EOF

    dpkg-deb --build --root-owner-group "$deb_root" "$out_deb"
    test -f "$out_deb"
    echo "  installer -> $out_deb"
    rm -rf "$deb_root"
    ;;

  *)
    echo "ERROR: unknown OS_LABEL=$OS_LABEL" >&2
    exit 1
    ;;
esac

echo "==== outputs for ${OS_LABEL}-${ARCH_LABEL} ===="
ls -la "$OUT_ROOT/portable"/*"${OS_LABEL}-${ARCH_LABEL}"* 2>/dev/null || true
ls -la "$OUT_ROOT/installer"/*"${OS_LABEL}-${ARCH_LABEL}"* 2>/dev/null || true
