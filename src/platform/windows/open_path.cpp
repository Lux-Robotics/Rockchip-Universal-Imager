#include "core/open_path.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace rui {

bool open_path_in_file_manager(const std::string& path) {
    // ShellExecuteA takes the native narrow (ANSI) path that path.string()
    // produces on Windows; ">32" is the documented success threshold.
    const HINSTANCE result =
        ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

} // namespace rui
