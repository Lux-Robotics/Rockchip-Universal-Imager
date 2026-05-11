#include "usb_driver_windows.h"
#include "logging.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#if defined(_WIN32) && defined(HAVE_LIBWDI)
#include <algorithm>
#include <cctype>
#include <libwdi.h>
#include <shellapi.h>
#include <windows.h>

namespace usb_driver {
namespace {

constexpr unsigned short kVid = 0x2207;
constexpr unsigned short kPid = 0x350b;
constexpr char kDefaultDeviceName[] = "Rockchip Bootloader Device";
constexpr char kVendorName[] = "hardware-helper";

bool is_running_as_admin() {
    BOOL is_admin = FALSE;
    PSID admin_group = nullptr;
    SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&nt_authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admin_group)) {
        CheckTokenMembership(nullptr, admin_group, &is_admin);
        FreeSid(admin_group);
    }
    return is_admin == TRUE;
}

std::wstring utf8_to_wide(const std::string& input) {
    if (input.empty()) {
        return L"";
    }
    int size = MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, nullptr, 0);
    if (size <= 0) {
        return std::wstring(input.begin(), input.end());
    }
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, out.data(), size);
    out.resize(static_cast<size_t>(size - 1));
    return out;
}

std::wstring quote_argument(const std::wstring& arg) {
    if (arg.find_first_of(L" \t\"") == std::wstring::npos) {
        return arg;
    }
    std::wstring quoted = L"\"";
    for (wchar_t ch : arg) {
        if (ch == L'\"') {
            quoted += L"\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += L"\"";
    return quoted;
}

bool is_libusb_win32_driver(const char* driver) {
    if (!driver) {
        return false;
    }
    std::string name(driver);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return name.find("libusb-win32") != std::string::npos || name.find("libusb0") != std::string::npos;
}

wdi_device_info* find_device(wdi_device_info* list) {
    for (auto* device = list; device != nullptr; device = device->next) {
        if (device->vid == kVid && device->pid == kPid) {
            return device;
        }
    }
    return nullptr;
}

DriverInfo build_info(wdi_device_info* device) {
    DriverInfo info;
    info.device_found = device != nullptr;
    if (!device) {
        info.error_message = "device not found";
        return info;
    }

    if (device->driver) {
        info.driver_name = device->driver;
    } else {
        info.driver_name = "(none)";
    }

    info.is_libusb_win32 = is_libusb_win32_driver(device->driver);
    if (!info.is_libusb_win32) {
        info.error_message = "driver is " + info.driver_name + " (expected libusb-win32)";
    }

    return info;
}

InstallResult run_elevated_install(const std::string& device_name) {
    InstallResult result;

    std::wstring exe_path(MAX_PATH, L'\0');
    DWORD exe_len = GetModuleFileNameW(nullptr, exe_path.data(), static_cast<DWORD>(exe_path.size()));
    if (exe_len == 0) {
        result.error_message = "unable to locate executable for elevation";
        logging::write("driver", "Elevation failed: could not resolve executable path.");
        return result;
    }
    exe_path.resize(exe_len);

    std::wstring params = L"--install-driver";
    if (!device_name.empty()) {
        params += L" --device-name ";
        params += quote_argument(utf8_to_wide(device_name));
    }

    std::wstring working_dir = std::filesystem::current_path().wstring();

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exe_path.c_str();
    sei.lpParameters = params.c_str();
    sei.lpDirectory = working_dir.empty() ? nullptr : working_dir.c_str();
    sei.nShow = SW_SHOWNORMAL;

    logging::write("driver", "Requesting administrator elevation for driver install.");
    if (!ShellExecuteExW(&sei)) {
        const DWORD err = GetLastError();
        if (err == ERROR_CANCELLED) {
            result.error_message = "administrator elevation was canceled";
            logging::write("driver", "Elevation canceled by user.");
        } else {
            result.error_message = "failed to request administrator privileges";
            logging::write("driver", "Elevation failed with error code " + std::to_string(err) + ".");
        }
        return result;
    }

    if (!sei.hProcess) {
        result.error_message = "elevated process did not start";
        logging::write("driver", "Elevation failed: no process handle returned.");
        return result;
    }

    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD exit_code = 1;
    if (!GetExitCodeProcess(sei.hProcess, &exit_code)) {
        exit_code = 1;
    }
    CloseHandle(sei.hProcess);

    if (exit_code != 0) {
        result.error_message = "driver installation failed in elevated process";
        logging::write("driver", "Elevated driver install failed with exit code " + std::to_string(exit_code) + ".");
        return result;
    }

    logging::write("driver", "Elevated driver install completed successfully.");
    result.success = true;
    return result;
}

InstallResult perform_install(const std::string& device_name) {
    InstallResult result;

    logging::write("driver", "Starting libusb-win32 driver installation.");

    wdi_device_info* list = nullptr;
    wdi_options_create_list options{};
    options.list_all = TRUE;

    int err = wdi_create_list(&list, &options);
    if (err != WDI_SUCCESS) {
        result.error_message = wdi_strerror(err);
        logging::write("driver", "wdi_create_list failed: " + result.error_message);
        return result;
    }

    wdi_device_info* device = find_device(list);
    if (!device) {
        wdi_destroy_list(list);
        result.error_message = "device not found";
        logging::write("driver", "Device not found for VID 0x2207 PID 0x350b.");
        return result;
    }

    const std::string applied_name = device_name.empty() ? kDefaultDeviceName : device_name;
    logging::write("driver", "Using device description: " + applied_name);
    if (device->desc) {
        free(device->desc);
        device->desc = nullptr;
    }
    device->desc = _strdup(applied_name.c_str());
    if (!device->desc) {
        wdi_destroy_list(list);
        result.error_message = "failed to allocate device name";
        logging::write("driver", "Failed to allocate device description string.");
        return result;
    }

    if (device->driver) {
        logging::write("driver", "Current driver: " + std::string(device->driver));
    }

    const auto driver_dir = std::filesystem::current_path() / "driver";
    std::filesystem::create_directories(driver_dir);
    const std::string driver_path = driver_dir.string();
    const std::string inf_name = "libusb-win32.inf";

    logging::write("driver", "Preparing driver files in " + driver_path + ".");
    wdi_options_prepare_driver prepare{};
    prepare.driver_type = WDI_LIBUSB0;
    prepare.vendor_name = const_cast<char*>(kVendorName);

    err = wdi_prepare_driver(device, driver_path.c_str(), inf_name.c_str(), &prepare);
    if (err != WDI_SUCCESS) {
        wdi_destroy_list(list);
        result.error_message = wdi_strerror(err);
        logging::write("driver", "wdi_prepare_driver failed: " + result.error_message);
        return result;
    }

    logging::write("driver", "Installing driver.");
    wdi_options_install_driver install{};
    err = wdi_install_driver(device, driver_path.c_str(), inf_name.c_str(), &install);
    if (err != WDI_SUCCESS) {
        wdi_destroy_list(list);
        result.error_message = wdi_strerror(err);
        logging::write("driver", "wdi_install_driver failed: " + result.error_message);
        return result;
    }

    wdi_destroy_list(list);
    result.success = true;
    logging::write("driver", "Driver installation successful.");
    return result;
}

} // namespace

DriverInfo query_driver() {
    wdi_device_info* list = nullptr;
    wdi_options_create_list options{};
    options.list_all = TRUE;

    const int err = wdi_create_list(&list, &options);
    if (err != WDI_SUCCESS) {
        DriverInfo info;
        info.error_message = wdi_strerror(err);
        return info;
    }

    wdi_device_info* device = find_device(list);
    DriverInfo info = build_info(device);
    wdi_destroy_list(list);
    return info;
}

InstallResult install_libusb_win32(const InstallOptions& options) {
    const std::string device_name = options.device_name.empty() ? kDefaultDeviceName : options.device_name;
    if (!is_running_as_admin()) {
        if (!options.allow_elevation) {
            InstallResult result;
            result.error_message = "administrator privileges required";
            logging::write("driver", "Driver install requires administrator privileges.");
            return result;
        }
        return run_elevated_install(device_name);
    }

    return perform_install(device_name);
}

} // namespace usb_driver

#else

namespace usb_driver {

DriverInfo query_driver() {
    DriverInfo info;
    info.error_message = "libwdi not available on this platform";
    return info;
}

InstallResult install_libusb_win32(const InstallOptions&) {
    InstallResult result;
    result.error_message = "libwdi not available on this platform";
    return result;
}

} // namespace usb_driver

#endif
