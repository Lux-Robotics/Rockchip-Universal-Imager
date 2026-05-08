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
