#!/usr/bin/env bash
set -euo pipefail

# Update CMake via Kitware APT repo (Ubuntu/Debian)
sudo apt update
sudo apt install -y ca-certificates gnupg lsb-release wget

if [ ! -f /usr/share/keyrings/kitware-archive-keyring.gpg ]; then
  wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc \
    | gpg --dearmor \
    | sudo tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null
fi

if [ ! -f /etc/apt/sources.list.d/kitware.list ]; then
  echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ $(lsb_release -cs) main" \
    | sudo tee /etc/apt/sources.list.d/kitware.list >/dev/null
fi

sudo apt update
sudo apt install -y cmake

cmake --version
