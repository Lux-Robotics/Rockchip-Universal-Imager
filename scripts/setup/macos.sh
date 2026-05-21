#!/usr/bin/env bash
set -euo pipefail

xcode-select --install || true

brew update
brew list cmake >/dev/null 2>&1 || brew install cmake
brew list ninja >/dev/null 2>&1 || brew install ninja
brew list pkgconf >/dev/null 2>&1 || brew install pkgconf
brew list libusb >/dev/null 2>&1 || brew install libusb
brew list autoconf >/dev/null 2>&1 || brew install autoconf
brew list automake >/dev/null 2>&1 || brew install automake
brew list libtool >/dev/null 2>&1 || brew install libtool

echo "macOS runner setup complete."
