#pragma once

#include <string>

namespace usb_driver {

struct DriverInfo {
    bool device_found = false;
    bool is_libusb_win32 = false;
    std::string driver_name;
    std::string error_message;
};

struct InstallResult {
    bool success = false;
    std::string error_message;
};

struct InstallOptions {
    std::string device_name;
    bool allow_elevation = true;
};

DriverInfo query_driver();
InstallResult install_libusb_win32(const InstallOptions& options);

} // namespace usb_driver

#ifdef _WIN32
namespace win_driver {

bool try_handle_driver_install_cli(int& exit_code);

} // namespace win_driver
#endif
