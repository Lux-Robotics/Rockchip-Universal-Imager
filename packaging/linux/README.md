# Linux packaging

## Build-server bootstrap

On a Debian/Ubuntu self-hosted runner (SSH):

```bash
bash packaging/linux/bootstrap-build-deps.sh
# optional: --skip-tauri-cli  --skip-cross
```

Installs apt packages (build tools, libusb, WebKitGTK/Tauri deps), aarch64
cross GCC, rustup + targets, and `tauri-cli`.

Shared CI path helpers (used by GitHub Actions bash steps on all OSes):
`packaging/ci/ci-env.sh`.

## Installer wrappers (future)

CI ships a flat install-layout zip:

```
rockchip-universal-imager-linux-x86_64/
  rockchip-universal-imager
  rkdeveloptool
  loader_binaries/
  README.txt
```

Optional later: `.deb` / AppImage / `.desktop` that installs that folder under
`/opt/rockchip-universal-imager/` and ships `99-rk-rockusb.rules` from the
rkdeveloptool submodule.
