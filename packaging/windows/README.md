# Windows packaging assets

Runtime packaging for Windows is driven by CPack in the top-level `CMakeLists.txt`
(NSIS installer and ZIP portable). WebView2 Evergreen remains an external
dependency and is not bundled.

Place future Windows-only packaging inputs here (icons, license RTF, NSIS
snippets) so they stay with the other platform assets under `packaging/`.
