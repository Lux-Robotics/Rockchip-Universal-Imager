# Rockchip Universal Imager

Cross-platform Rockchip flashing helper, implemented with **Rust + Tauri 2**.

This branch is a **from-scratch rewrite** that aims to match the behaviour of the
former C++/Saucer application. The GUI is native Rust; **`rkdeveloptool` remains
a separate C++ binary** that the app spawns (same product model as before).

## Layout

```
ui/                              # Web frontend (HTML/JS/CSS)
src-tauri/                       # Tauri/Rust package
packaging/                       # Bootstrap scripts + NSIS/DMG/deb helpers
loader_binaries/                 # SPL loader blobs for portable / install trees
dependencies/rkdeveloptool/      # Git submodule: Lux-Robotics/rkdeveloptool
```

Clone with submodules:

```bash
git clone --recurse-submodules <this-repo-url>
git submodule update --init --recursive
```

## Port status

| Area | Status |
|------|--------|
| Shell (window, dialogs, single-instance, logs) | Done |
| `rkdeveloptool` spawn + progress | Done (external C++ binary) |
| USB hotplug | Done on Unix; Windows partial |
| Connect / flash / erase / backup / storage | Done (behavioural port) |
| Linux udev install | Done |
| Windows USB + libwdi driver install | Not yet |

## Product packaging

### Portable (zip)

One zip **per OS/arch** containing only:

| Platform | Contents |
|----------|----------|
| **macOS** | `Rockchip Universal Imager.app` + `rkdeveloptool` + `loader_binaries/` |
| **Windows / Linux** | `rockchip-universal-imager[.exe]` + `rkdeveloptool[.exe]` + `loader_binaries/` |

No `portable` marker file, no README. Extract and run.

### Installer (real installers)

One installer **per OS/arch**:

| Platform | Format | Installs to |
|----------|--------|-------------|
| **Windows** | NSIS `.exe` | `%ProgramFiles%\Rockchip Universal Imager\` |
| **macOS** | `.dmg` (drag folder to Applications) | `/Applications/Rockchip Universal Imager/` (`.app` + `rkdeveloptool` + `loader_binaries/`) |
| **Linux** | `.deb` | `/opt/rockchip-universal-imager/` + desktop entry |

### Logs (always system dirs)

Portable and installed builds both write logs to:

| OS | Path |
|----|------|
| Windows | `%LOCALAPPDATA%\RockchipUniversalImager\logs` |
| macOS | `~/Library/Logs/RockchipUniversalImager` |
| Linux | `${XDG_STATE_HOME:-~/.local/state}/rockchip-universal-imager/logs` |

## CI workflows

| Workflow | Role |
|----------|------|
| `build-rkdeveloptool.yaml` | Static `rkdeveloptool` companions (6 OS/arch) |
| `build-app.yaml` | Tauri app (5 cells; **no linux-aarch64 GUI** yet) |
| **`package.yaml`** | Runs both builds, then packages **portable + installer** per cell |

Self-hosted runners (`[self-hosted, Linux|Windows|macOS, X64]`). Bootstraps:

```bash
bash packaging/linux/bootstrap-build-deps.sh
bash packaging/macos/bootstrap-build-deps.sh
# Windows (elevated PowerShell):
.\packaging\windows\bootstrap-build-deps.ps1
```

Windows packaging needs **NSIS** (`makensis`); bootstrap installs it via winget.

## Local develop (macOS example)

```bash
brew install libusb
rustup update stable
cargo install tauri-cli --version "^2" --locked
cargo tauri dev
```

Place `rkdeveloptool` next to the debug binary or next to the `.app` for release runs.
