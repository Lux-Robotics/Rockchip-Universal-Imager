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

### Product layouts

**Portable** and **installer payload** use the **same folder shape** (two
binaries side by side + loaders). Only the zip wrapper differs:

```
rockchip-universal-imager…/
  rockchip-universal-imager[.exe]
  rkdeveloptool[.exe]          # static single-file companion
  loader_binaries/
  portable                     # portable zip only (empty marker file)
  README.txt                   # installer payload only
```

- **Portable** — run from that folder (no system install). Marker file
  `portable` selects portable path behaviour for companions.
- **Installer payload** — same two apps as a folder; a future NSIS/DMG/deb
  copies that folder into OS program directories. No `portable` marker.

**Logs** always go to OS user/system locations (portable *and* installed):

| OS | Log directory |
|----|----------------|
| Windows | `%LOCALAPPDATA%\RockchipUniversalImager\logs` |
| macOS | `~/Library/Logs/RockchipUniversalImager` |
| Linux | `${XDG_STATE_HOME:-~/.local/state}/rockchip-universal-imager/logs` |

### CI workflows (compile once → package)

| Workflow | Role |
|----------|------|
| `build-rkdeveloptool.yaml` | Compile static companions (6 OS/arch) |
| `build-app.yaml` | Compile Tauri app only (6 OS/arch) |
| **`package.yaml`** | Runs **both** builds, then zips portable + installer for all cells |

**Self-hosted** runners; bootstraps under `packaging/*/bootstrap-build-deps.*`.

Typical release: **Actions → Package → Run workflow**.

Iterate faster with **Build rkdeveloptool** or **Build app** alone.

Local assemble (after you have both binaries):

```bash
# Stage like CI:
mkdir -p dist/dev && cp app rkdeveloptool dist/dev/ && cp -R loader_binaries dist/dev/
# Portable:
: > dist/dev/portable
# Logs still go to the system paths above, not dist/dev/logs
```

Native installers (NSIS / DMG / deb) can wrap the installer payload later;
stubs live under `packaging/{windows,macos,linux}/`.
