#!/usr/bin/env bash
# Build libusb-1.0 from source with a (cross) MinGW toolchain and install it to
# a prefix. Used by CI for the Windows arm64 build: there's no prebuilt arm64
# libusb usable from an x64 host, so the arm64 rkdeveloptool build links this
# one. The compiler is picked up from --host via whatever is on PATH (e.g. an
# llvm-mingw cross toolchain), mirroring how macOS builds its arm64 libusb in
# the workflow rather than assuming a pre-built copy on the runner.
set -euo pipefail

HOST="$1"                  # e.g. aarch64-w64-mingw32
PREFIX="$2"                # install prefix (unix path)
VERSION="${3:-1.0.27}"

if [[ -z "$HOST" || -z "$PREFIX" ]]; then
  echo "build_libusb_mingw: usage: <host-triple> <prefix> [version]" >&2
  exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

curl -fL -o "$WORK/libusb.tar.bz2" \
  "https://github.com/libusb/libusb/releases/download/v${VERSION}/libusb-${VERSION}.tar.bz2"
tar -xjf "$WORK/libusb.tar.bz2" -C "$WORK" --strip-components=1

cd "$WORK"
rm -rf "$PREFIX"
./configure --host="$HOST" --prefix="$PREFIX"
make -j"$(nproc 2>/dev/null || echo 1)"
make install
