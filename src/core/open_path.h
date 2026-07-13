#pragma once

#include <string>

namespace rui {

// Reveal a directory/file in the OS file manager (Finder / Explorer / XDG).
// Best-effort: returns false if the opener could not be launched or failed.
bool open_path_in_file_manager(const std::string& path);

} // namespace rui
