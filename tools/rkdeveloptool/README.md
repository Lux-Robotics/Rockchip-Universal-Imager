# rkdeveloptool (C++ companion)

Not part of the Tauri crate. The GUI **spawns** this binary; users can also run
it directly from a terminal in the portable zip.

## Source

Upstream (project default): https://github.com/lux-robotics/rkdeveloptool

Build with autotools on macOS/Linux, or MSYS2 / llvm-mingw on Windows. Optional
helper scripts in `scripts/` are conveniences only—they are not required by the
Rust app build.

## Portable layout

Place the built binary next to the Tauri app:

| OS | File name |
|----|-----------|
| Windows | `rkdeveloptool.exe` |
| macOS / Linux | `rkdeveloptool` |

Then run `scripts/package-portable.sh` with `RKDEV_BIN` set, or copy by hand
into the zip folder with an empty `portable` marker file.
