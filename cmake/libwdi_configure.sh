#!/usr/bin/env bash
set -euo pipefail

SRC_DIR="$(cygpath -u "$1")"
PREFIX="$(cygpath -u "$2")"
LIBUSB0_DIR="$(cygpath -u "$3")"
MSYSTEM_NAME="$4"

export MSYSTEM="$MSYSTEM_NAME"
case "$MSYSTEM_NAME" in
  MINGW64) ROOT=/mingw64 ;;
  CLANGARM64) ROOT=/clangarm64 ;;
  *) ROOT=/mingw64 ;;
 esac
export PATH="$ROOT/bin:/usr/bin"

SITE="/tmp/libwdi_site"
printf "WDK_DIR=\nwith_wdkdir=\nLIBUSBK_DIR=\nwith_libusbk=\nenable_32bit=no\nenable_64bit=yes\n" > "$SITE"
export CONFIG_SITE="$SITE"
export WDK_DIR=
export with_wdkdir=
export LIBUSBK_DIR=
export with_libusbk=

cd "$SRC_DIR"
./autogen.sh
./configure --prefix="$PREFIX" --with-wdkdir= --with-libusbk= --with-libusb0="$LIBUSB0_DIR" --enable-static=yes --enable-shared=no --disable-32bit --enable-64bit
