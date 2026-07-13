# Rockchip Universal Imager

Cross-platform Rockchip flashing helper, implemented with **Rust + Tauri 2**.

This branch is a **from-scratch rewrite** that aims to match the behaviour of the
former C++/Saucer application. The GUI is native Rust; **`rkdeveloptool` remains
a separate C++ binary** that the app spawns (same product model as before).

## Layout

```
apps/imager-tauri/     # Tauri app (Rust backend + web UI)
tools/rkdeveloptool/   # How to obtain/build the C++ companion CLI
scripts/               # Portable zip packaging
loader_binaries/       # SPL loader blobs shipped with portable builds
packaging/             # Future installer assets (DMG/desktop/etc.)
```

## Develop

```bash
# Prerequisites: Rust stable, cargo-tauri (v2), platform webview deps
cargo install tauri-cli --version "^2" --locked

cd apps/imager-tauri
cargo tauri dev --manifest-path src-tauri/Cargo.toml
```

## Portable package (primary distribution for now)

```bash
cd apps/imager-tauri
cargo tauri build --manifest-path src-tauri/Cargo.toml --no-bundle

# Build or download rkdeveloptool separately, then:
RKDEV_BIN=/path/to/rkdeveloptool ./scripts/package-portable.sh
```

Zip contents:

```
rockchip-universal-imager[.exe]
rkdeveloptool[.exe]    # separate program; still usable from a terminal
portable               # empty marker → logs next to the extract folder
loader_binaries/       # optional
```

Installers (NSIS/DMG/DEB) can be added later; they are intentionally off by default.

## Docs

- `apps/imager-tauri/README.md` — Tauri build system, CI notes, design choices
- `tools/rkdeveloptool/README.md` — companion CLI
- `PORT_TAURI.md` — port overview
