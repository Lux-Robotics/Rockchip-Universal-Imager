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
- **Portable zip / DMG contents** (companions **beside** the `.app`, not inside it):
  - `Rockchip Universal Imager.app`
  - `rkdeveloptool`
  - `loader_binaries/`
- **Installer DMG:** same three items + Applications symlink

The GUI looks for `rkdeveloptool` and `loader_binaries/` in the directory that
contains the `.app`. Users must keep that layout after install.

Gatekeeper / first-open steps for unsigned builds: see root `README.md`.

Logs: `~/Library/Logs/RockchipUniversalImager`
