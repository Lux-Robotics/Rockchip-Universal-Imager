#include "core/device_access.h"

namespace device_access {

Status query() {
    // macOS talks to Rockchip devices via libusb directly; no driver/udev step.
    Status status;
    status.kind = Kind::none;
    status.device_relevant = true;
    status.ready = true;
    return status;
}

InstallResult install(const InstallOptions& /*options*/) {
    InstallResult result;
    result.error_message = "device access setup is not required on this platform";
    return result;
}

bool try_handle_elevated_cli(int& /*exit_code*/) {
    return false;
}

} // namespace device_access
