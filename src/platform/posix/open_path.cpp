#include "core/open_path.h"

#include <string>
#include <vector>

#include <reproc++/reproc.hpp>

namespace rui {

bool open_path_in_file_manager(const std::string& path) {
#if defined(__APPLE__)
    std::vector<std::string> args = {"open", path};
#else
    std::vector<std::string> args = {"xdg-open", path};
#endif
    reproc::process process;
    reproc::options options;
    if (process.start(args, options)) {
        return false;
    }
    // These launchers hand off to the file manager and exit promptly, so
    // waiting is cheap and lets us report a real failure (non-zero exit).
    const auto [status, wait_ec] = process.wait(reproc::infinite);
    return !wait_ec && status == 0;
}

} // namespace rui
