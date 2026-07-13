# rkdeveloptool (C++ companion)

This tool **stays C++** and is **not** part of the Rust/Tauri crate.

## Portable layout (v1)

Zip contents (same folder):

```
rockchip-universal-imager      # or .exe on Windows / .app on macOS later
rkdeveloptool                  # or rkdeveloptool.exe
portable                       # empty marker file
loader_binaries/               # optional SPL loaders
```

Build `rkdeveloptool` with the existing autotools/MSYS scripts (see repo-root
`cmake/` helpers and CI). Stage the binary next to the Tauri app when packaging
via `scripts/package-portable.mjs`.

The GUI only **spawns** this binary; it never links against its sources.
