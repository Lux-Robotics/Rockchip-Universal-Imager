#!/usr/bin/env bash
set -euo pipefail

SRC_DIR="$1"
PREFIX="$2"
BIN_NAME="$3"

if [[ -z "$SRC_DIR" || -z "$PREFIX" || -z "$BIN_NAME" ]]; then
  echo "rkdev_msys: missing arguments" >&2
  exit 1
fi

cd "$SRC_DIR"

if [[ -f "./configure" ]]; then
  sh ./configure --prefix="$PREFIX"
else
  if [[ -x "./autogen.sh" ]]; then
    ./autogen.sh
  else
    autoreconf -i
  fi
  sh ./configure --prefix="$PREFIX"
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
