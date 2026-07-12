#pragma once

#include <string>

namespace udev_rules {

struct RulesInfo {
    bool installed = false;
    std::string error;
};

// True if the app's udev rules file is already present.
RulesInfo query();

struct InstallResult {
    bool success = false;
    std::string error_message;
};

// Writes the Rockchip udev rule (non-root access to VID 0x2207 USB devices)
// to /etc/udev/rules.d and reloads udev, elevating via pkexec. Blocks until
// the polkit auth dialog is resolved - call from a worker thread, never the
// UI thread.
InstallResult install();

} // namespace udev_rules
