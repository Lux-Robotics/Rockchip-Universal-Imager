# Packaging assets

Per-OS install/portable packaging inputs live here. CMake/CPack rules that
consume them remain in the top-level `CMakeLists.txt` (build system, not runtime).

| Directory | Contents |
|-----------|----------|
| `macos/` | `Info.plist`, entitlements for the `.app` / notarization |
| `linux/` | `.desktop` entry for DEB/RPM installers |
| `windows/` | Notes and future NSIS/icon assets |

Portable builds install an empty `portable` marker next to the app so logging
stays self-contained; that marker is generated at configure/package time, not
stored here.
