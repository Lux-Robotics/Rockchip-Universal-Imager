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
- **Embed companions** (done by `package-cell.sh`):
  - `Contents/MacOS/rkdeveloptool`
  - `Contents/Resources/loader_binaries/`
- **Portable zip:** `.app` + `Allow and Open.command`
- **Installer DMG:** `.app` + Applications symlink + `Allow and Open.command`

Companions are embedded so App Translocation and single-item install still work.
Do not rely on loose files next to the `.app` for release builds.

### End-user Gatekeeper helper

Builds are **not Apple-notarized**. Ship `packaging/macos/Allow and Open.command`
next to the app. Users should:

1. Drag **Rockchip Universal Imager.app** into **Applications**
2. Double-click **Allow and Open.command** (clears quarantine, refreshes ad-hoc
   signature if possible, opens the app)

Terminal equivalent:

```bash
xattr -dr com.apple.quarantine "/Applications/Rockchip Universal Imager.app"
open "/Applications/Rockchip Universal Imager.app"
```

Logs: `~/Library/Logs/RockchipUniversalImager`
