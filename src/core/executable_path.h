#pragma once

#include <filesystem>

namespace rui {

// Directory containing the running executable. GUI apps launched via
// Finder/desktop launchers don't inherit a useful working directory, so
// executable-relative paths must be used instead of cwd. Use companion_dir()
// for files intentionally shipped beside a macOS .app.
std::filesystem::path executable_dir();

// Directory containing runtime companions. For a macOS .app this is the
// directory beside the bundle; elsewhere it is executable_dir(). Keeping
// this separate lets the app fail clearly when its externally shipped files
// are removed instead of accidentally using stale in-bundle copies.
std::filesystem::path companion_dir();

// Directory for bundled non-executable data (loader .bin files). Inside a
// macOS .app bundle this is Contents/Resources - codesign refuses to seal a
// bundle with non-Mach-O files in Contents/MacOS, so data can't live next to
// the executable there. Everywhere else it's simply executable_dir().
std::filesystem::path resource_dir();

} // namespace rui
