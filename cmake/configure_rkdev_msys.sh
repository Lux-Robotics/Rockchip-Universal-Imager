#!/usr/bin/env bash
set -euo pipefail

SRC_DIR="$1"
PREFIX="$2"

if [[ -z "$SRC_DIR" || -z "$PREFIX" ]]; then
  echo "configure_rkdev_msys: missing arguments" >&2
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
