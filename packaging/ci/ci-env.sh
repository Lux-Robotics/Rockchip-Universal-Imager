# Shared path/env helpers for CI bash steps (Linux / macOS / MSYS2 Windows).
# Part of packaging/ (same family as bootstrap-build-deps.* scripts).
#
#   source packaging/ci/ci-env.sh
#
# Goals:
#   - No hard dependency on OneDrive / Desktop / a fixed drive letter
#   - Discover MSYS2 / llvm-mingw / Homebrew via env + common locations
#   - Normalize Windows paths to Unix form for bash

# Convert Windows path to Unix (/c/Users/...) when needed.
ci_to_unix_path() {
  local p="${1:-}"
  [[ -n "$p" ]] || { echo ""; return 0; }

  if command -v cygpath >/dev/null 2>&1; then
    cygpath -u "$p" 2>/dev/null && return 0
  fi

  # C:\foo or C:/foo → /c/foo
  if [[ "$p" =~ ^[A-Za-z]:[\\/].* || "$p" =~ ^[A-Za-z]:$ ]]; then
    local drive rest
    drive="$(printf '%s' "${p:0:1}" | tr 'A-Z' 'a-z')"
    rest="${p:2}"
    rest="${rest//\\//}"
    printf '/%s%s\n' "$drive" "$rest"
    return 0
  fi

  # Already unix-ish or relative
  printf '%s\n' "${p//\\//}"
}

# Resolve repo workspace (Actions always sets GITHUB_WORKSPACE).
ci_workspace() {
  local raw="${GITHUB_WORKSPACE:-${GITHUB_WORKSPACE_RAW:-}}"
  if [[ -z "$raw" ]]; then
    # Fallback: script-relative walk is unreliable; use pwd
    raw="$(pwd)"
  fi
  ci_to_unix_path "$raw"
}

# Append dir to PATH if it exists and is not already present.
ci_path_prepend() {
  local d="$1"
  [[ -n "$d" && -d "$d" ]] || return 0
  case ":${PATH:-}:" in
    *":$d:"*) ;;
    *) export PATH="$d${PATH:+:$PATH}" ;;
  esac
}

# Discover MSYS2 root (Windows only). Prints unix path or empty.
ci_find_msys2_root() {
  local candidates=()

  if [[ -n "${MSYS2_ROOT:-}" ]]; then
    candidates+=("$(ci_to_unix_path "$MSYS2_ROOT")")
  fi
  if [[ -n "${MSYS2_BASH:-}" ]]; then
    # .../usr/bin/bash.exe → root is two levels up
    local b
    b="$(ci_to_unix_path "$MSYS2_BASH")"
    if [[ -x "$b" || -f "$b" ]]; then
      candidates+=("$(cd "$(dirname "$b")/../.." && pwd)")
    fi
  fi

  # SystemDrive (e.g. C:) when running under MSYS
  local sd="${SYSTEMDRIVE:-${SystemDrive:-C:}}"
  candidates+=("$(ci_to_unix_path "${sd}/msys64")")
  candidates+=(/c/msys64 /d/msys64 /e/msys64 /msys64)

  local c
  for c in "${candidates[@]}"; do
    [[ -n "$c" ]] || continue
    if [[ -x "$c/usr/bin/bash" || -f "$c/usr/bin/bash.exe" || -d "$c/mingw64/bin" ]]; then
      printf '%s\n' "$c"
      return 0
    fi
  done
  return 1
}

# Discover llvm-mingw root (Windows aarch64 cross). Prints unix path or empty.
ci_find_llvm_mingw_root() {
  local candidates=()

  if [[ -n "${LLVM_MINGW_ROOT:-}" ]]; then
    candidates+=("$(ci_to_unix_path "$LLVM_MINGW_ROOT")")
  fi

  local sd="${SYSTEMDRIVE:-${SystemDrive:-C:}}"
  candidates+=("$(ci_to_unix_path "${sd}/llvm-mingw")")
  candidates+=(/c/llvm-mingw /d/llvm-mingw /e/llvm-mingw /llvm-mingw)
  candidates+=("$(ci_to_unix_path "${PROGRAMFILES:-}/llvm-mingw")")

  local c
  for c in "${candidates[@]}"; do
    [[ -n "$c" ]] || continue
    if [[ -x "$c/bin/aarch64-w64-mingw32-gcc" || -f "$c/bin/aarch64-w64-mingw32-gcc.exe" ]]; then
      printf '%s\n' "$c"
      return 0
    fi
  done
  return 1
}

# Set PATH / ACLOCAL for the current OS_LABEL (linux|macos|windows).
ci_setup_toolchain_path() {
  local os="${1:-${OS_LABEL:-}}"
  case "$os" in
    windows)
      local msys llvm cargo_u
      msys="$(ci_find_msys2_root || true)"
      if [[ -n "$msys" ]]; then
        export MSYS2_ROOT_UNIX="$msys"
        ci_path_prepend "$msys/usr/bin"
        ci_path_prepend "$msys/mingw64/bin"
        export ACLOCAL_PATH="${msys}/usr/share/aclocal:${msys}/mingw64/share/aclocal${ACLOCAL_PATH:+:$ACLOCAL_PATH}"
        # pkg-config data for MinGW libusb
        export PKG_CONFIG_PATH="${msys}/mingw64/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
        echo "ci-env: MSYS2_ROOT_UNIX=$msys"
      else
        echo "ci-env: WARNING: MSYS2 root not found (set MSYS2_ROOT)" >&2
      fi

      llvm="$(ci_find_llvm_mingw_root || true)"
      if [[ -n "$llvm" ]]; then
        export LLVM_MINGW_ROOT_UNIX="$llvm"
        ci_path_prepend "$llvm/bin"
        echo "ci-env: LLVM_MINGW_ROOT_UNIX=$llvm"
      else
        echo "ci-env: WARNING: llvm-mingw not found (set LLVM_MINGW_ROOT) — windows-aarch64 may fail" >&2
      fi

      # Cargo for Tauri steps under MSYS bash
      if [[ -n "${USERPROFILE:-}" ]]; then
        cargo_u="$(ci_to_unix_path "${USERPROFILE}/.cargo/bin")"
        ci_path_prepend "$cargo_u"
      elif [[ -n "${HOME:-}" ]]; then
        ci_path_prepend "${HOME}/.cargo/bin"
      fi
      ;;
    macos)
      if [[ -x /opt/homebrew/bin/brew ]]; then
        # shellcheck disable=SC1091
        eval "$(/opt/homebrew/bin/brew shellenv)"
      elif [[ -x /usr/local/bin/brew ]]; then
        # shellcheck disable=SC1091
        eval "$(/usr/local/bin/brew shellenv)"
      fi
      ci_path_prepend "${HOME:-}/.cargo/bin"
      ;;
    linux)
      ci_path_prepend "${HOME:-}/.cargo/bin"
      ;;
  esac
}
