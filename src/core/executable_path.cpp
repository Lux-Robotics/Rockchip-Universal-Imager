#include "core/executable_path.h"

#include <stdexcept>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#else
#include <climits>
#include <unistd.h>
#endif

namespace rui {

std::filesystem::path executable_dir() {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        throw std::runtime_error("failed to resolve executable path");
    }
    return std::filesystem::path(buffer, buffer + len).parent_path();
#elif defined(__APPLE__)
    char buffer[PATH_MAX];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) != 0) {
        throw std::runtime_error("failed to resolve executable path");
    }
    return std::filesystem::canonical(buffer).parent_path();
#else
    char buffer[PATH_MAX];
    const ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len == -1) {
        throw std::runtime_error("failed to resolve executable path");
    }
    buffer[len] = '\0';
    return std::filesystem::path(buffer).parent_path();
#endif
}

std::filesystem::path companion_dir() {
    const auto executable = executable_dir();
#if defined(__APPLE__)
    // <directory>/rockchip-universal-imager.app/Contents/MacOS -> <directory>. Only
    // apply this layout rule to a real bundle; a bare development executable
    // still uses files in its own directory.
    const auto contents = executable.parent_path();
    const auto bundle = contents.parent_path();
    if (executable.filename() == "MacOS" &&
        contents.filename() == "Contents" &&
        bundle.extension() == ".app") {
        return bundle.parent_path();
    }
#endif
    return executable;
}

std::filesystem::path resource_dir() {
#if defined(__APPLE__)
    // <bundle>/Contents/MacOS -> <bundle>/Contents/Resources. Fall back to
    // the executable directory when running as a bare (non-bundled) binary.
    const auto resources = executable_dir().parent_path() / "Resources";
    if (std::filesystem::exists(resources)) {
        return resources;
    }
#endif
    return executable_dir();
}

} // namespace rui
