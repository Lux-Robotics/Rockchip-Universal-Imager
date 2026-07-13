#include "core/single_instance.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace rui {

bool try_acquire_single_instance() {
    // A named mutex is the idiomatic Windows single-instance primitive. The
    // handle is intentionally leaked for the process lifetime; the OS frees
    // it (and the name) when the process exits.
    static HANDLE mutex = nullptr;
    mutex = CreateMutexW(nullptr, TRUE, L"Local\\RockchipUniversalImagerSingleInstance");
    if (mutex == nullptr) {
        return true; // fail open
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        mutex = nullptr;
        return false;
    }
    return true;
}

void notify_already_running() {
    MessageBoxW(nullptr,
                L"Rockchip Universal Imager is already running.\n\n"
                L"Only one instance can run at a time. Switch to the window that's already open.",
                L"Rockchip Universal Imager",
                MB_OK | MB_ICONINFORMATION);
}

} // namespace rui
