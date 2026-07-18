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

## Installer wrappers (future)

CI ships a flat install-layout zip (two binaries + `loader_binaries/`), not a
`.app` bundle. That matches `paths::companion_dir` for a non-bundled binary.

Optional later:

- Wrap the same payload next to a `.app` (companions live **beside** the `.app`)
- Or build a `.dmg` that contains the folder from `package-installer.sh`
