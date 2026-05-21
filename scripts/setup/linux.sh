#!/usr/bin/env bash
set -euo pipefail

sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  git \
  ninja-build \
  pkg-config \
  libgtk-3-dev \
  libwebkit2gtk-4.1-dev \
  libudev-dev \
  libusb-1.0-0-dev \
  dh-autoreconf \
  autoconf \
  automake \
  libtool

echo "Linux runner setup complete."
