#pragma once

#include <filesystem>

namespace hwhelper {

// Directory containing the running executable. GUI apps launched via
// Finder/desktop launchers don't inherit a working directory next to the
// executable, so anything shipped alongside the app (rkdeveloptool, loader
// binaries, drivers) must be located relative to this instead of cwd.
std::filesystem::path executable_dir();

} // namespace hwhelper
