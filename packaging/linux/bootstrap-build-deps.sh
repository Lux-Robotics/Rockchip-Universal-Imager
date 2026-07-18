#!/usr/bin/env bash
# Bootstrap Linux build-server deps for Rockchip Universal Imager + rkdeveloptool.
#
# Debian/Ubuntu (apt). Run over SSH as a sudo-capable user (not as root for rustup).
# Self-hosted selection: [self-hosted, Linux, X64] (any hostname, e.g. Ubuntu-24)
#
#   bash packaging/linux/bootstrap-build-deps.sh
#   bash packaging/linux/bootstrap-build-deps.sh --skip-tauri-cli
#   bash packaging/linux/bootstrap-build-deps.sh --skip-cross
#
# ---------------------------------------------------------------------------
# Runner expectations (workflows assume these are pre-installed)
# ---------------------------------------------------------------------------
# Used by:
#   .github/workflows/build-rkdeveloptool.yaml  (linux-x86_64, linux-aarch64)
#   .github/workflows/portable.yml / installer.yml
#
# Required for rkdeveloptool (autogen + configure + make):
#   - build-essential (gcc, g++, make)
#   - pkg-config, autoconf, automake, libtool, m4, dh-autoreconf
#   - libusb-1.0-0-dev, libudev-dev  (headers + static .a when available)
#   - curl, wget, tar, bzip2          (fetch/build libusb from source if needed)
#   - git, zip, unzip, file, ca-certificates
#   - aarch64 cross (unless --skip-cross):
#       gcc-aarch64-linux-gnu, g++-aarch64-linux-gnu, binutils-aarch64-linux-gnu,
#       libc6-dev-arm64-cross
#
# Required for Tauri app (portable/installer):
#   - libssl-dev
#   - libwebkit2gtk-4.1-dev, libayatana-appindicator3-dev, librsvg2-dev, patchelf
#   - rustup + stable (1.85+), targets x86_64-unknown-linux-gnu (+ aarch64)
#   - tauri-cli ^2 (optional: --skip-tauri-cli; CI can cargo-install)
#
# Installs all of the above.
#
set -euo pipefail

SKIP_TAURI_CLI=0
SKIP_CROSS=0
for arg in "$@"; do
  case "$arg" in
    --skip-tauri-cli) SKIP_TAURI_CLI=1 ;;
    --skip-cross)     SKIP_CROSS=1 ;;
    -h|--help)
      sed -n '2,22p' "$0"
      exit 0
      ;;
    *)
      echo "Unknown option: $arg" >&2
      exit 2
      ;;
  esac
done

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "This script is for Linux only." >&2
  exit 1
fi

if [[ "$(id -u)" -eq 0 ]]; then
  echo "Do not run the whole script as root (rustup should own your user home)." >&2
  echo "Run as a normal user with sudo privileges." >&2
  exit 1
fi

if ! command -v sudo >/dev/null 2>&1; then
  echo "sudo is required." >&2
  exit 1
fi

if ! command -v apt-get >/dev/null 2>&1; then
  echo "This script expects apt-get (Debian/Ubuntu). Adapt for other distros." >&2
  exit 1
fi

log() { printf '\n==> %s\n' "$*"; }
have() { command -v "$1" >/dev/null 2>&1; }

# ---------------------------------------------------------------------------
# System packages (apt)
# ---------------------------------------------------------------------------
install_apt_packages() {
  log "Updating apt indices…"
  sudo apt-get update

  local pkgs=(
    # core build (rkdeveloptool + Tauri)
    build-essential
    curl
    wget
    file
    git
    ca-certificates
    pkg-config
    autoconf
    automake
    libtool
    libtool-bin
    m4
    dh-autoreconf
    make
    # libusb source tarball (.tar.bz2) + packaging
    tar
    bzip2
    zip
    unzip
    # USB / crypto for app + rkdeveloptool
    libusb-1.0-0-dev
    libudev-dev
    libssl-dev
    # Tauri 2 / WebKitGTK
    libwebkit2gtk-4.1-dev
    libayatana-appindicator3-dev
    librsvg2-dev
    patchelf
  )

  if [[ "$SKIP_CROSS" -eq 0 ]]; then
    pkgs+=(
      gcc-aarch64-linux-gnu
      g++-aarch64-linux-gnu
      binutils-aarch64-linux-gnu
      libc6-dev-arm64-cross
      # Cross pkg-config helper when available (Debian/Ubuntu)
      pkg-config
    )
  fi

  log "Installing apt packages…"
  sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${pkgs[@]}"

  # aarch64 multiarch for Tauri GUI cross (libdbus-sys, gtk, webkit2gtk).
  # Without these, `cargo build --target aarch64-unknown-linux-gnu` fails in pkg-config.
  if [[ "$SKIP_CROSS" -eq 0 ]]; then
    log "Installing aarch64 multiarch Tauri/WebKit deps (best-effort)…"
    sudo dpkg --add-architecture arm64 || true
    sudo apt-get update || true
    local arm_pkgs=(
      libusb-1.0-0-dev:arm64
      libdbus-1-dev:arm64
      libssl-dev:arm64
      libgtk-3-dev:arm64
      libwebkit2gtk-4.1-dev:arm64
      libayatana-appindicator3-dev:arm64
      librsvg2-dev:arm64
      libsoup-3.0-dev:arm64
      libjavascriptcoregtk-4.1-dev:arm64
    )
    for p in "${arm_pkgs[@]}"; do
      sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "$p" 2>/dev/null \
        || echo "  (skipped $p — install manually if linux-aarch64 app build fails)"
    done
  fi
}

# ---------------------------------------------------------------------------
# Rust
# ---------------------------------------------------------------------------
install_rust() {
  if have rustup; then
    log "rustup already present"
  else
    log "Installing rustup (stable, default)…"
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
      | sh -s -- -y --default-toolchain stable --profile default
  fi

  # shellcheck source=/dev/null
  source "$HOME/.cargo/env"

  log "Updating stable toolchain…"
  rustup toolchain install stable --profile default
  rustup default stable
  rustup update stable

  log "Adding Linux targets (host + aarch64)…"
  rustup target add x86_64-unknown-linux-gnu
  if [[ "$SKIP_CROSS" -eq 0 ]]; then
    rustup target add aarch64-unknown-linux-gnu
  fi

  # Default linker for aarch64 cross when using the apt cross gcc
  local cargo_cfg="$HOME/.cargo/config.toml"
  if [[ "$SKIP_CROSS" -eq 0 ]] && have aarch64-linux-gnu-gcc; then
    if [[ ! -f "$cargo_cfg" ]] || ! grep -q 'aarch64-unknown-linux-gnu' "$cargo_cfg" 2>/dev/null; then
      log "Writing aarch64 linker hint to $cargo_cfg"
      mkdir -p "$HOME/.cargo"
      {
        echo ""
        echo "# Added by packaging/linux/bootstrap-build-deps.sh"
        echo "[target.aarch64-unknown-linux-gnu]"
        echo 'linker = "aarch64-linux-gnu-gcc"'
      } >>"$cargo_cfg"
    fi
  fi

  log "Rust versions:"
  rustc -vV
  cargo -vV
  rustup show
}

install_tauri_cli() {
  # shellcheck source=/dev/null
  source "$HOME/.cargo/env"
  log "Installing tauri-cli ^2 (cargo install)…"
  cargo install tauri-cli --version "^2" --locked
  cargo tauri --version || true
}

# ---------------------------------------------------------------------------
# Verify
# ---------------------------------------------------------------------------
verify() {
  # shellcheck source=/dev/null
  [[ -f "$HOME/.cargo/env" ]] && source "$HOME/.cargo/env"

  log "Verification"
  local ok=1
  check() {
    local name="$1"
    shift
    if "$@"; then
      printf '  OK  %s\n' "$name"
    else
      printf '  FAIL %s\n' "$name"
      ok=0
    fi
  }

  check "gcc"            have gcc
  check "g++"            have g++
  check "make"           have make
  check "git"            have git
  check "curl"           have curl
  check "bzip2"          have bzip2
  check "tar"            have tar
  check "pkg-config"     have pkg-config
  check "libusb (pc)"    pkg-config --exists libusb-1.0
  check "libudev (pc)"   pkg-config --exists libudev
  check "libusb static"  bash -c 'd=$(pkg-config --variable=libdir libusb-1.0 2>/dev/null); [[ -n "$d" && -f "$d/libusb-1.0.a" ]]'
  check "webkit2gtk"     pkg-config --exists webkit2gtk-4.1
  check "autoconf"       have autoconf
  check "automake"       have automake
  check "autoreconf"     have autoreconf
  check "libtoolize"     have libtoolize
  check "m4"             have m4
  check "patchelf"       have patchelf
  check "zip"            have zip
  check "unzip"          have unzip
  check "rustup"         have rustup
  check "rustc"          have rustc
  check "cargo"          have cargo
  check "target x86_64"  rustup target list --installed | grep -q x86_64-unknown-linux-gnu
  if [[ "$SKIP_CROSS" -eq 0 ]]; then
    check "aarch64-linux-gnu-gcc" have aarch64-linux-gnu-gcc
    check "aarch64-linux-gnu-g++" have aarch64-linux-gnu-g++
    check "target aarch64"        rustup target list --installed | grep -q aarch64-unknown-linux-gnu
    check "dbus-1 arm64 pc"       bash -c 'PKG_CONFIG_PATH=/usr/lib/aarch64-linux-gnu/pkgconfig pkg-config --exists dbus-1'
  fi
  if [[ "$SKIP_TAURI_CLI" -eq 0 ]]; then
    check "tauri-cli"    cargo tauri --version
  fi

  if [[ "$ok" -eq 1 ]]; then
    log "All checks passed."
    echo
    echo "Satisfies runner expectations for:"
    echo "  build-rkdeveloptool.yaml (linux-x86_64, linux-aarch64)"
    echo "  portable.yml / installer.yml (Tauri + rkdeveloptool)"
    echo
    echo "Next (in the repo):"
    echo "  git submodule update --init --recursive"
    echo "  # rkdeveloptool: ./autogen.sh && ./configure && make"
    echo "  cargo tauri build --no-bundle --target x86_64-unknown-linux-gnu"
    if [[ "$SKIP_CROSS" -eq 0 ]]; then
      echo "  cargo tauri build --no-bundle --target aarch64-unknown-linux-gnu"
    fi
  else
    log "Some checks failed — see FAIL lines above."
    exit 1
  fi
}

main() {
  log "Linux build-dep bootstrap (user=$(id -un), arch=$(uname -m))"
  install_apt_packages
  install_rust
  if [[ "$SKIP_TAURI_CLI" -eq 0 ]]; then
    install_tauri_cli
  else
    log "Skipping tauri-cli (--skip-tauri-cli)"
  fi
  verify
}

main
