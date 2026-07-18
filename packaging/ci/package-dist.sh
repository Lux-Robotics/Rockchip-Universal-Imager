#!/usr/bin/env bash
# Assemble portable + installer payload zips from prebuilt app + rkdeveloptool.
#
# Layout (same folder shape for both; only the zip wrapper / marker differ):
#
#   <folder>/
#     rockchip-universal-imager[.exe]
#     rkdeveloptool[.exe]
#     loader_binaries/
#     portable          # portable zip only (empty marker)
#     README.txt        # installer zip only
#
# Logs are NOT packaged; the app writes logs to OS system dirs always.
#
# Usage (from repo root, after artifacts are extracted under dist/in/):
#   packaging/ci/package-dist.sh
#
# Expected inputs:
#   dist/in/rkdeveloptool-<os>-<arch>/rkdeveloptool[.exe]
#   dist/in/app-<os>-<arch>/rockchip-universal-imager[.exe]
#   loader_binaries/   (from checkout)
#
# Outputs:
#   dist/out/portable/rockchip-universal-imager-portable-<os>-<arch>.zip
#   dist/out/installer/rockchip-universal-imager-<os>-<arch>-install.zip

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

IN_ROOT="${IN_ROOT:-$ROOT/dist/in}"
OUT_ROOT="${OUT_ROOT:-$ROOT/dist/out}"
LOADER_SRC="${LOADER_SRC:-$ROOT/loader_binaries}"

# Matches build-app.yaml products (linux-aarch64 GUI omitted for now).
CELLS=(
  "linux x86_64 rockchip-universal-imager rkdeveloptool"
  "windows x86_64 rockchip-universal-imager.exe rkdeveloptool.exe"
  "windows aarch64 rockchip-universal-imager.exe rkdeveloptool.exe"
  "macos x86_64 rockchip-universal-imager rkdeveloptool"
  "macos aarch64 rockchip-universal-imager rkdeveloptool"
)

need_file() {
  local f="$1"
  if [[ ! -f "$f" ]]; then
    echo "ERROR: missing $f" >&2
    return 1
  fi
}

find_one() {
  # find_one <dir> <name> [alt name...]
  local dir="$1"
  shift
  local n
  for n in "$@"; do
    if [[ -f "$dir/$n" ]]; then
      echo "$dir/$n"
      return 0
    fi
    # one nesting level (artifact zip layout)
    local c
    c="$(find "$dir" -maxdepth 2 -type f -name "$n" 2>/dev/null | head -1 || true)"
    if [[ -n "$c" && -f "$c" ]]; then
      echo "$c"
      return 0
    fi
  done
  return 1
}

package_cell() {
  local os="$1" arch="$2" app_name="$3" rk_name="$4"

  local app_dir="${IN_ROOT}/app-${os}-${arch}"
  local rk_dir="${IN_ROOT}/rkdeveloptool-${os}-${arch}"

  echo "==> package ${os}-${arch}"

  if [[ ! -d "$app_dir" ]]; then
    echo "  SKIP: no app artifact at $app_dir"
    return 0
  fi
  if [[ ! -d "$rk_dir" ]]; then
    echo "  SKIP: no rkdeveloptool artifact at $rk_dir"
    return 0
  fi

  local app_bin rk_bin
  app_bin="$(find_one "$app_dir" "$app_name" "rockchip-universal-imager.exe" "rockchip-universal-imager")"
  rk_bin="$(find_one "$rk_dir" "$rk_name" "rkdeveloptool.exe" "rkdeveloptool")"
  need_file "$app_bin"
  need_file "$rk_bin"

  local stage_base="${OUT_ROOT}/staging/${os}-${arch}"
  rm -rf "$stage_base"
  mkdir -p "$stage_base/loader_binaries"

  cp "$app_bin" "$stage_base/$app_name"
  cp "$rk_bin" "$stage_base/$rk_name"
  chmod +x "$stage_base/$app_name" "$stage_base/$rk_name" 2>/dev/null || true

  if [[ -d "$LOADER_SRC" ]]; then
    cp -R "$LOADER_SRC"/. "$stage_base/loader_binaries/" 2>/dev/null || true
  fi

  # --- Portable: same folder + empty `portable` marker ---
  local port_folder="rockchip-universal-imager-portable-${os}-${arch}"
  local port_stage="${OUT_ROOT}/portable/staging/${port_folder}"
  local port_zip="${OUT_ROOT}/portable/${port_folder}.zip"
  rm -rf "$port_stage"
  mkdir -p "$(dirname "$port_stage")"
  cp -R "$stage_base" "$port_stage"
  : >"$port_stage/portable"
  mkdir -p "$(dirname "$port_zip")"
  (
    cd "${OUT_ROOT}/portable/staging"
    rm -f "$port_zip"
    zip -r "$port_zip" "$port_folder"
  )
  echo "  portable -> $port_zip"

  # --- Installer payload: same folder + README (no portable marker) ---
  # Native installers later unpack this folder into OS program dirs.
  local inst_folder="rockchip-universal-imager-${os}-${arch}"
  local inst_stage="${OUT_ROOT}/installer/staging/${inst_folder}"
  local inst_zip="${OUT_ROOT}/installer/${inst_folder}-install.zip"
  rm -rf "$inst_stage"
  mkdir -p "$(dirname "$inst_stage")"
  cp -R "$stage_base" "$inst_stage"
  {
    echo "Rockchip Universal Imager — install layout (${os}/${arch})"
    echo ""
    echo "This folder is the install payload: keep these side by side:"
    echo "  - ${app_name}"
    echo "  - ${rk_name}"
    echo "  - loader_binaries/"
    echo ""
    echo "An installer should place this folder under the OS application"
    echo "directory (e.g. Program Files / Applications / /opt) and leave"
    echo "the relative layout intact."
    echo ""
    echo "Logs are written to the OS user log location (not this folder):"
    echo "  Windows: %LOCALAPPDATA%\\RockchipUniversalImager\\logs"
    echo "  macOS:   ~/Library/Logs/RockchipUniversalImager"
    echo "  Linux:   \${XDG_STATE_HOME:-~/.local/state}/rockchip-universal-imager/logs"
  } >"$inst_stage/README.txt"
  mkdir -p "$(dirname "$inst_zip")"
  (
    cd "${OUT_ROOT}/installer/staging"
    rm -f "$inst_zip"
    zip -r "$inst_zip" "$inst_folder"
  )
  echo "  installer -> $inst_zip"
}

main() {
  echo "package-dist: ROOT=$ROOT"
  echo "  IN_ROOT=$IN_ROOT"
  echo "  OUT_ROOT=$OUT_ROOT"
  mkdir -p "$OUT_ROOT/portable" "$OUT_ROOT/installer"

  local cell
  for cell in "${CELLS[@]}"; do
    # shellcheck disable=SC2086
    package_cell $cell
  done

  echo "==== portable zips ===="
  ls -la "$OUT_ROOT/portable"/*.zip 2>/dev/null || echo "(none)"
  echo "==== installer zips ===="
  ls -la "$OUT_ROOT/installer"/*.zip 2>/dev/null || echo "(none)"
}

main "$@"
