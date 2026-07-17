# Rockchip Universal Imager

Cross-platform Rockchip flashing helper, implemented with **Rust + Tauri 2**.

This branch is a **from-scratch rewrite** that aims to match the behaviour of the
former C++/Saucer application. The GUI is native Rust; **`rkdeveloptool` remains
a separate C++ binary** that the app spawns (same product model as before).

## Layout

```
ui/                              # Web frontend (HTML/JS/CSS)
src-tauri/                       # Tauri/Rust package
packaging/                       # Portable zip script + future installer assets
loader_binaries/                 # SPL loader blobs for portable builds
dependencies/rkdeveloptool/      # Git submodule: Lux-Robotics/rkdeveloptool (C++ CLI source)
```

Clone with submodules:

```bash
git clone --recurse-submodules <this-repo-url>
# or after a normal clone:
git submodule update --init --recursive
```

Run `cargo tauri` from the **repo root** (it discovers `src-tauri/tauri.conf.json`).
Or `cd src-tauri && cargo tauri dev` (the Tauri package directory).

Build the companion from the submodule, then stage the binary next to the app (see below).
## Port status

| Area | Status (macOS arm64 / Linux x64) |
|------|----------------------------------|
| Shell (window, dialogs, single-instance, logs) | Done |
| `rkdeveloptool` spawn + progress | Done (external C++ binary) |
| USB hotplug (libusb/`rusb`) | Done on Unix |
| Connect / flash / erase / backup / storage | Done (behavioural port) |
| Linux udev install | Done |
| Windows USB + libwdi driver install | Not yet |

## Develop on macOS arm64

```bash
# Tools
xcode-select --install   # if needed
brew install libusb
rustup update stable
cargo install tauri-cli --version "^2" --locked

cd /Users/antho/Desktop/hardware-helper

# Dev window (rebuilds Rust on change)
cargo tauri dev
```

### Put `rkdeveloptool` where the app finds it

```bash
# After a release-style build:
cargo tauri build --no-bundle

# Staging folder for a quick test (portable layout)
STAGE=dist/dev-run
mkdir -p "$STAGE/loader_binaries"
cp src-tauri/target/release/rockchip-universal-imager "$STAGE/"
cp /path/to/rkdeveloptool "$STAGE/rkdeveloptool"
chmod +x "$STAGE/rkdeveloptool"
cp -R loader_binaries/* "$STAGE/loader_binaries/" 2>/dev/null || true
: > "$STAGE/portable"
cd "$STAGE" && ./rockchip-universal-imager
```

During `cargo tauri dev`, the binary lives under `src-tauri/target/debug/`. Place
`rkdeveloptool` **next to that binary**:

```bash
cp /path/to/rkdeveloptool src-tauri/target/debug/rkdeveloptool
chmod +x src-tauri/target/debug/rkdeveloptool
```

### Linux x64

System packages (Debian/Ubuntu-style): `libwebkit2gtk-4.1-dev`, `libusb-1.0-0-dev`,
build-essential, etc. Same `cargo tauri dev` from the repo root.

## Packaging layout

Both **portable** and **installer** products use the same folder shape the app
already resolves (`paths.rs`): two executables side by side plus loaders.

```
rockchip-universal-imager…/
  rockchip-universal-imager[.exe]
  rkdeveloptool[.exe]
  loader_binaries/
  portable          # portable zip only (empty marker file)
  README.txt        # installer payload only
```

Packaging is defined **only in GitHub Actions** (no `packaging/*.sh` scripts):

| Workflow | Artifact |
|----------|----------|
| `.github/workflows/portable.yml` | zip + `portable` marker |
| `.github/workflows/installer.yml` | install-layout zip (no marker) |

**Self-hosted runners** only. System toolchains are assumed pre-installed
(full lists live in comments at the top of each workflow). CI only installs
Rust pieces (`rustup` targets, `tauri-cli`, Cargo crates).

Each OS runner builds **that OS only**, for host arch **and** aarch64:

| Runner label | Artifacts |
|--------------|-----------|
| `[self-hosted, Linux, X64]` | `linux-x86_64`, `linux-aarch64` |
| `[self-hosted, Windows, X64]` | `windows-x86_64`, `windows-aarch64` |
| `[self-hosted, macOS, X64]` | `macos-x86_64`, `macos-aarch64` |

Per matrix cell:

1. Build `rkdeveloptool` (autotools; MinGW / llvm-mingw on Windows)
2. `cargo tauri build --no-bundle --target <triple>`
3. Stage folder + zip (inline in the workflow)
4. Upload artifact

Local equivalent (any OS with bash, for debugging):

```bash
# C++ companion (Unix example)
cd dependencies/rkdeveloptool && ./autogen.sh && ./configure && make
# Tauri app
cargo tauri build --no-bundle
# Then copy both binaries + loader_binaries/ into a folder and zip
```

Native wrappers (NSIS / DMG / deb) can wrap the installer payload later;
stubs live under `packaging/{windows,macos,linux}/`.
