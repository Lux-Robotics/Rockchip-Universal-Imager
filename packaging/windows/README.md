# Windows packaging

## Build-server bootstrap

On the Windows CI host, **elevated PowerShell**:

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
.\packaging\windows\bootstrap-build-deps.ps1
# optional: -SkipTauriCli -SkipLlvmMingw -SkipVsBuildTools
```

From Git Bash / MSYS2 (still needs elevation for a full install):

```bash
bash packaging/windows/bootstrap-build-deps.sh
```

Installs Git/CMake/Ninja (winget), VS 2022 Build Tools (x64+ARM64), MSYS2
MinGW64 + libusb, llvm-mingw (arm64 rkdeveloptool), rustup + MSVC targets,
and `tauri-cli`. Sets machine env `MSYS2_ROOT`, `MSYS2_BASH`, `LLVM_MINGW_ROOT`.

### CI helpers

Shared bash helpers live in `packaging/ci/ci-env.sh` (workspace normalize, tool
PATH discovery). Windows workflow steps invoke MSYS2 bash via the `MSYS2_BASH`
machine env (set by this bootstrap), not a custom `.cmd` launcher.

## Installer wrappers (future)

CI currently ships an **install-layout zip**:

```
rockchip-universal-imager-windows-x86_64/
  rockchip-universal-imager.exe
  rkdeveloptool.exe
  libusb-1.0.dll          # when built with MinGW
  loader_binaries/
  README.txt
```

A real installer (NSIS or WiX MSI) should extract that folder under
`%LocalAppData%\Programs\Rockchip Universal Imager\` (or similar) and
create a Start Menu shortcut to `rockchip-universal-imager.exe`.

Tauri’s built-in bundler (`nsis` / `msi`) can replace this once
`externalBin` + `resources` are wired for `rkdeveloptool` and loaders.
