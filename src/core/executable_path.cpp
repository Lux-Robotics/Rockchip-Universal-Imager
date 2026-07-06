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

namespace hwhelper {

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

} // namespace hwhelper
