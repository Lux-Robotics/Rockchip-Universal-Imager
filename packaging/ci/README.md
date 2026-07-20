# CI helpers

| File | Role |
|------|------|
| `ci-env.sh` | PATH / workspace helpers for self-hosted bash steps |
| `package-cell.sh` | One OS/arch: portable zip + real installer (NSIS / DMG / deb) |

## Workflows

```
package.yaml
  ├─ build-rkdeveloptool.yaml  → rkdeveloptool-<os>-<arch>
  ├─ build-app.yaml            → app-<os>-<arch>  (.app on macOS)
  └─ package matrix (5 cells)  → portable-* + installer-* artifacts
```

### Portable zip contents

- **macOS:** `.app` + `Allow and Open.command` (companions embedded in the bundle)
- **Windows/Linux:** app binary + `rkdeveloptool` + `loader_binaries/`

No `portable` marker.

### Installers

| OS | Tool | Output |
|----|------|--------|
| Windows | NSIS (`makensis`) | `*-setup.exe` |
| macOS | `hdiutil` | `*.dmg` (`.app` + Applications link + `Allow and Open.command`) |
| Linux | `dpkg-deb` | `*.deb` → `/opt/rockchip-universal-imager` |

### App matrix note

`linux-aarch64` GUI is not built on x86_64 hosts (WebKit cross-link). Companion
`rkdeveloptool-linux-aarch64` is still produced by **Build rkdeveloptool**.
