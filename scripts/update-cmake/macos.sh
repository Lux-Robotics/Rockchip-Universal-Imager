#!/usr/bin/env bash
set -euo pipefail

brew update
brew upgrade cmake || brew install cmake

cmake --version
