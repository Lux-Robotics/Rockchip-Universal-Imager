# Rockchip Universal Imager (Tauri / Rust)

Nested app under `apps/imager-tauri/`. Companion tool docs: `tools/rkdeveloptool/`.

## How Tauri’s build system works

Tauri is a **two-language desktop shell**:

| Piece | Role |
|-------|------|
| **Frontend** | Static web UI (`ui/`) or a JS bundler (Vite, etc.). We use **vanilla HTML/JS** — no Vite required. |
| **Rust crate** (`src-tauri/`) | Native backend: commands, process spawn, USB, FS, OS integration. |
| **`cargo tauri` CLI** | Orchestrates: optional frontend build → `cargo build` of the Rust crate → optional **bundler** (DMG/NSIS/DEB). |
| **WebView** | OS webview (WebView2 / WKWebView / webkit2gtk), not Chromium embedded by default. |

### Important commands

```bash
cd apps/imager-tauri

# Dev: compile Rust + load ui/ in a window
cargo tauri dev --manifest-path src-tauri/Cargo.toml

# Release binary only (no installer) — portable path
cargo tauri build --manifest-path src-tauri/Cargo.toml --no-bundle

# Zip: app + rkdeveloptool + portable marker
RKDEV_BIN=/path/to/rkdeveloptool node ../../scripts/package-portable.mjs
```

`tauri.conf.json` has `"bundle": { "active": false }` so **installers are off by default**.  
Portable-first product: a **zip** with:

```
rockchip-universal-imager[.exe]
rkdeveloptool[.exe]          # separate C++ program, still useful CLI
portable                     # empty marker → logs beside the app
loader_binaries/             # optional
```

Installers (NSIS/DMG/DEB) can be enabled later without changing the app logic.

### What Cargo does vs what scripts do

- **Cargo/Tauri** builds the **GUI process** only.
- **`rkdeveloptool` (C++)** is built by existing autotools/MSYS/cmake helpers under repo `cmake/` / CI — **not** by `cargo build`.
- **`scripts/package-portable.mjs`** copies both into one zip.

### GitHub Actions integration

Tauri fits GHA cleanly; the mismatch is **companions**, not the GUI:

| Job step | Fits Tauri? |
|----------|-------------|
| `actions/checkout` | Yes |
| Install Rust (`dtolnay/rust-toolchain`) | Yes |
| Install Node (only if you use a JS bundler; optional for us) | Optional |
| Linux: `webkit2gtk`, `libusb`, build deps | Yes (system packages) |
| `cargo tauri build --no-bundle` | Yes — produces the app binary |
| **Build C++ rkdeveloptool** (MSYS / autotools / llvm-mingw) | **Separate step** — same as today |
| Zip app + rkdeveloptool + `portable` | Custom script (we provide) |
| `tauri-apps/tauri-action` for installers | Optional later; not needed for portable zip |
| Code signing / notarization | Optional later for installers |

**Cross-compile:** Tauri’s official path is **native runners** (macOS runner for arm64/x64 mac, Windows runner for Windows, Linux runner for Linux). Cross-compiling the GUI (e.g. Linux→Windows) is painful; keep **cross only for rkdeveloptool** if needed (llvm-mingw), build GUI natively.

Sketch:

```yaml
jobs:
  portable:
    strategy:
      matrix:
        include:
          - os: macos-latest
          - os: ubuntu-22.04
          - os: windows-latest
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - uses: dtolnay/rust-toolchain@stable
      # Linux webkit/libusb packages…
      - name: Build C++ rkdeveloptool
        run: … # existing scripts → out/rkdeveloptool
      - name: Build Tauri app (no installer)
        working-directory: apps/imager-tauri
        run: cargo tauri build --manifest-path src-tauri/Cargo.toml --no-bundle
      - name: Portable zip
        env:
          RKDEV_BIN: ${{ github.workspace }}/out/rkdeveloptool
        run: node scripts/package-portable.mjs
      - uses: actions/upload-artifact@v4
        with:
          name: portable-${{ matrix.os }}
          path: dist/portable/*.zip
```

**Bottom line:** Tauri owns **GUI compile + optional installers**. Your **zip-of-two-binaries** model is a thin CI script on top — same philosophy as today’s portable packages, simpler shell.

---

## D3 — Tokio vs `std::process` (decision note)

| | `std::process` + threads | Tokio |
|--|--------------------------|--------|
| **Who spawns rkdeveloptool?** | `std::process::Command` | `tokio::process::Command` |
| **Reading stdout** | Background `std::thread` + pipes | Async tasks |
| **Binary size** | No extra async runtime **from us** | Tokio runtime is large **if linked** |
| **Reality with Tauri 2** | Tauri still pulls async deps for the shell; app binary is multi‑MB either way | Often **already** in the graph transitively |
| **Runtime cost** | Thread per long operation | Polling runtime; fine for one flasher process |
| **Code shape** | Explicit threads; easy to reason about | `async fn` commands; cancellation tokens |

**How `std::process` works when “most code is Rust”:**  
The UI is web; native logic is Rust. Flashing = Rust spawns **external C++** `rkdeveloptool` the same way C++ did with reproc. No need for async runtimes just to run a subprocess:

```text
JS invoke("flash_image") → Rust command → std::process::Command("rkdeveloptool")
                                         → thread reads stdout lines / \r progress
                                         → app.emit("flash-progress", …)
```

**Default for this port:** start with **`std::process` + threads** for rkdev. Add Tokio later only if we need richer async orchestration. Do **not** link Tokio solely for process I/O.

*(Tauri’s own runtime may still appear in `Cargo.lock`; we simply avoid depending on it for our domain code.)*

---

## D4 — USB: try rusb everywhere first

Use **`rusb` (libusb C)** hotplug on Unix **and** attempt on Windows. If Windows hotplug is unreliable, fall back to Win32 `WM_DEVICECHANGE` under `platform/windows/`.

---

## D14 — Nested monorepo

```
apps/imager-tauri/     # this app
tools/rkdeveloptool/   # docs + future staging for C++ tool
scripts/               # portable zip, CI helpers
```

Legacy C++ Saucer tree remains at repo root `src/` until cutover.

---

## Dev prerequisites

- Rust stable
- `cargo install tauri-cli --version "^2"`
- macOS: Xcode CLT  
- Linux: `libwebkit2gtk-4.1-dev`, `libusb-1.0-0-dev`, …  
- Windows: WebView2, MSVC  

Node is only required for `package-portable.mjs` (and optional npm scripts), not for compiling the GUI if you invoke `cargo tauri` directly.
