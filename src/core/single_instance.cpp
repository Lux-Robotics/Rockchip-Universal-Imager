#include "core/single_instance.h"

#include <filesystem>
#include <string>

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#else

#include <cstdio>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#endif

namespace hwhelper {

#if defined(_WIN32)

bool try_acquire_single_instance() {
    // A named mutex is the idiomatic Windows single-instance primitive. The
    // handle is intentionally leaked for the process lifetime; the OS frees
    // it (and the name) when the process exits.
    static HANDLE mutex = nullptr;
    mutex = CreateMutexW(nullptr, TRUE, L"Local\\HardwareHelperSingleInstance");
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
                L"Hardware Helper is already running.",
                L"Hardware Helper",
                MB_OK | MB_ICONINFORMATION);
}

#else

bool try_acquire_single_instance() {
    // Held for the process lifetime: the fd is intentionally never closed on
    // the success path, so the flock stays held until the process exits (the
    // kernel releases it then, even on a crash - no stale-lock cleanup).
    static int lock_fd = -1;

    std::error_code ec;
    const auto path = std::filesystem::temp_directory_path(ec) / "hardware-helper.lock";
    if (ec) {
        return true; // fail open
    }

    lock_fd = ::open(path.c_str(), O_CREAT | O_RDWR, 0644);
    if (lock_fd < 0) {
        return true; // fail open
    }

    if (::flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        // Another instance already holds the exclusive lock.
        ::close(lock_fd);
        lock_fd = -1;
        return false;
    }

    return true;
}

#if !defined(__APPLE__)
// macOS provides a native (NSAlert) implementation in
// single_instance_mac.mm; every other POSIX platform falls back to stderr.
void notify_already_running() {
    std::fprintf(stderr, "Hardware Helper is already running.\n");
}
#endif

#endif

} // namespace hwhelper
