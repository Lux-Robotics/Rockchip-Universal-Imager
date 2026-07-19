# macOS packaging

## Build-server bootstrap

On a macOS self-hosted runner (SSH):

```bash
bash packaging/macos/bootstrap-build-deps.sh
# optional: --skip-tauri-cli
```

Installs Xcode CLT, Homebrew packages (libusb, autotools), rustup + both
Apple targets, and `tauri-cli`.

Shared CI path helpers (used by GitHub Actions bash steps on all OSes):
`packaging/ci/ci-env.sh`.

## Packaging

- **App build:** `cargo tauri build --bundles app` → `Rockchip Universal Imager.app`
- **Portable zip:** `.app` + `rkdeveloptool` + `loader_binaries/` (companions beside the `.app`)
- **Installer DMG:** single **Rockchip Universal Imager** folder (`.app` + companions) plus
  an Applications shortcut, arrow background, and drag instructions

```bash
# After staging app + rkdeveloptool + loader_binaries into dist/stage:
packaging/macos/make-dmg.sh dist/stage dist/out/rockchip-universal-imager-macos-aarch64.dmg

# Regenerate the DMG window background (optional):
swift packaging/macos/generate-dmg-background.swift
```

DMG volume layout:

```
Rockchip Universal Imager/          ← drag this one folder into Applications
  Rockchip Universal Imager.app
  rkdeveloptool
  loader_binaries/
Applications -> /Applications
```

Logs: `~/Library/Logs/RockchipUniversalImager`
