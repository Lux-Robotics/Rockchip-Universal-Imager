# Tauri port (branch `port/tauri-rust`)

Active work lives under **`apps/imager-tauri/`**.  
See **`apps/imager-tauri/README.md`** for:

- How Tauri’s build system works
- How it integrates with GitHub Actions
- Portable zip layout (app + separate `rkdeveloptool`)
- D3 (`std::process` vs Tokio) and D4 (rusb-first USB)

```bash
cd apps/imager-tauri
cargo tauri dev --manifest-path src-tauri/Cargo.toml
# portable binary (no installer):
cargo tauri build --manifest-path src-tauri/Cargo.toml --no-bundle
RKDEV_BIN=/path/to/rkdeveloptool node ../../scripts/package-portable.mjs
```

C++ Saucer tree under `src/` remains until feature parity; `rkdeveloptool` stays C++ forever under `tools/rkdeveloptool/` docs + external build.
