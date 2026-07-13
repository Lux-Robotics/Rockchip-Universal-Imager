#pragma once

#include <string>

// OS-specific device access setup (Windows USB driver, Linux udev rules,
// nothing on macOS) behind one API so main/UI stay platform-neutral.
namespace device_access {

enum class Kind {
    none,           // macOS / unsupported: no install step
    windows_driver, // libusb-win32 via libwdi
    linux_udev,     // udev rules via pkexec
};

struct Status {
    Kind kind = Kind::none;
    // False when a device must be present to evaluate access (Windows) and
    // none was found. Always true for linux_udev/none.
    bool device_relevant = true;
    // True when access is already OK (correct driver / rules installed /
    // platform needs nothing).
    bool ready = true;
    std::string detail; // driver name, "installed", etc.
    std::string error;
};

struct InstallResult {
    bool success = false;
    std::string error_message;
};

struct InstallOptions {
    // Windows: optional device description passed to libwdi. Ignored elsewhere.
    std::string device_name;
};

Status query();
InstallResult install(const InstallOptions& options = {});

// Windows elevated re-exec entry (--install-driver). No-op elsewhere; returns
// false if this process was not launched as that CLI helper.
bool try_handle_elevated_cli(int& exit_code);

} // namespace device_access
