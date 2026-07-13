#!/usr/bin/env bash
set -euo pipefail

SRC_DIR="$1"
PREFIX="$2"
BIN_NAME="$3"
# Optional 4th arg: a cross host triple (e.g. aarch64-w64-mingw32). When set,
# this is an llvm-mingw cross build - configure runs with --host and the
# runtime DLLs are staged from the cross toolchain / prebuilt libusb (passed in
# via RKDEV_CROSS_LIBUSB_BIN and RKDEV_CROSS_RUNTIME_DLL_DIR) rather than from a
# native MSYSTEM prefix.
HOST_TRIPLE="${4:-}"

if [[ -z "$SRC_DIR" || -z "$PREFIX" || -z "$BIN_NAME" ]]; then
  echo "rkdev_msys: missing arguments" >&2
  exit 1
fi

cd "$SRC_DIR"

CONFIGURE_ARGS=(--prefix="$PREFIX")
if [[ -n "$HOST_TRIPLE" ]]; then
  # autoconf enters cross mode from --host and won't try to run target
  # (arm64) test binaries on the x64 build host.
  CONFIGURE_ARGS+=(--host="$HOST_TRIPLE")
fi

if [[ -f "./configure" ]]; then
  sh ./configure "${CONFIGURE_ARGS[@]}"
else
  if [[ -x "./autogen.sh" ]]; then
    ./autogen.sh
  else
    autoreconf -i
  fi
  sh ./configure "${CONFIGURE_ARGS[@]}"
fi

if [[ -f Makefile ]]; then
  sed -i 's/-Werror//g' Makefile
fi

make clean || true
JOBS=$(nproc 2>/dev/null || echo 1)
make -j"$JOBS"

mkdir -p "$PREFIX/bin"
if [[ -f "$SRC_DIR/$BIN_NAME" ]]; then
  cp -f "$SRC_DIR/$BIN_NAME" "$PREFIX/bin/$BIN_NAME"
elif [[ "$BIN_NAME" == *.exe && -f "$SRC_DIR/${BIN_NAME%.exe}" ]]; then
  cp -f "$SRC_DIR/${BIN_NAME%.exe}" "$PREFIX/bin/$BIN_NAME"
else
  echo "rkdeveloptool binary not found in $SRC_DIR" >&2
  exit 1
fi

if [[ -n "$HOST_TRIPLE" ]]; then
  # Cross build: ship the arm64 libusb DLL and llvm-mingw's arm64 C/C++ runtime
  # DLLs (libc++, libunwind, ...) next to the exe. Copy the whole runtime bin
  # dir - extra DLLs are harmless and the exact set varies by toolchain version.
  if [[ -n "${RKDEV_CROSS_LIBUSB_BIN:-}" && -d "${RKDEV_CROSS_LIBUSB_BIN}" ]]; then
    cp -f "$RKDEV_CROSS_LIBUSB_BIN"/*.dll "$PREFIX/bin/" 2>/dev/null || true
  fi
  if [[ -n "${RKDEV_CROSS_RUNTIME_DLL_DIR:-}" && -d "${RKDEV_CROSS_RUNTIME_DLL_DIR}" ]]; then
    cp -f "$RKDEV_CROSS_RUNTIME_DLL_DIR"/*.dll "$PREFIX/bin/" 2>/dev/null || true
  fi
  exit 0
fi

# ---- native build: copy the MSYSTEM toolchain's runtime DLLs ----
TOOLCHAIN_BIN=""
if command -v g++ >/dev/null 2>&1; then
  TOOLCHAIN_BIN=$(dirname "$(command -v g++)")
elif command -v clang++ >/dev/null 2>&1; then
  TOOLCHAIN_BIN=$(dirname "$(command -v clang++)")
elif command -v gcc >/dev/null 2>&1; then
  TOOLCHAIN_BIN=$(dirname "$(command -v gcc)")
elif command -v clang >/dev/null 2>&1; then
  TOOLCHAIN_BIN=$(dirname "$(command -v clang)")
fi

if [[ -z "$TOOLCHAIN_BIN" ]]; then
  case "${MSYSTEM:-}" in
    MINGW64) TOOLCHAIN_BIN="/mingw64/bin" ;;
    CLANGARM64) TOOLCHAIN_BIN="/clangarm64/bin" ;;
  esac
fi

DLLS=(
  "libstdc++-6.dll"
  "libgcc_s_seh-1.dll"
  "libusb-1.0.dll"
  "libwinpthread-1.dll"
)

for dll in "${DLLS[@]}"; do
  if [[ -n "$TOOLCHAIN_BIN" && -f "$TOOLCHAIN_BIN/$dll" ]]; then
    cp -f "$TOOLCHAIN_BIN/$dll" "$PREFIX/bin/$dll"
  fi
done
