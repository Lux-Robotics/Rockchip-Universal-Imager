#include "core/single_instance.h"

#include <cstdio>
#include <filesystem>
#include <string>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#if !defined(__APPLE__)
#include <cstdint>
#include <vector>

#include <reproc++/reproc.hpp>
#endif

namespace rui {

bool try_acquire_single_instance() {
    // Held for the process lifetime: the fd is intentionally never closed on
    // the success path, so the flock stays held until the process exits (the
    // kernel releases it then, even on a crash - no stale-lock cleanup).
    static int lock_fd = -1;

    std::error_code ec;
    const auto path = std::filesystem::temp_directory_path(ec) / "rockchip-universal-imager.lock";
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
// macOS provides a native NSAlert in platform/macos/single_instance.mm.
void notify_already_running() {
    reproc::process process;
    const std::vector<std::string> args = {
        "zenity",
        "--info",
        "--title=Rockchip Universal Imager",
        "--text=Rockchip Universal Imager is already running.\n\n"
        "Only one instance can run at a time. Switch to the window that's already open.",
        "--width=360",
    };
    reproc::options options;
    if (!process.start(args, options)) {
        const auto [status, wait_ec] = process.wait(reproc::infinite);
        (void)status;
        (void)wait_ec;
        return;
    }
    // zenity missing or failed to start - fall back so the user still sees something.
    std::fprintf(stderr, "Rockchip Universal Imager is already running.\n");
}
#endif

} // namespace rui
