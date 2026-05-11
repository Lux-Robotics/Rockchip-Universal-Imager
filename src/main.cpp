#include "webview/webview.h"
#include "rkdeveloptool_runner.h"
#include "logging.h"
#include "usb_driver_windows.h"
#include "webview_bindings.h"
#include "windows_cmdline.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32

#include <windows.h>

#endif

namespace {

std::atomic<bool> g_polling_enabled{true};
std::atomic<bool> g_polling_stop{false};
std::thread g_polling_thread;

void set_device_polling_enabled(bool enabled) {
    g_polling_enabled.store(enabled);
}

void start_device_polling(webview::webview& w) {
    if (g_polling_thread.joinable()) {
        return;
    }

    g_polling_thread = std::thread([&w]() {
        std::string last_output;

        while (!g_polling_stop.load()) {
            if (!g_polling_enabled.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }

            std::string output;
            std::mutex wait_mutex;
            std::condition_variable wait_cv;
            bool done = false;

            auto task = rkdev::start_rkdeveloptool(
                {"ld"},
                [&](const std::string& line) {
                    output += line + "\n";
                },
                [&](const rkdev::ProcessResult& result) {
                    if (!result.error_message.empty()) {
                        output += "Error: " + result.error_message + "\n";
                    }
                    {
                        std::lock_guard<std::mutex> lock(wait_mutex);
                        done = true;
                    }
                    wait_cv.notify_one();
                });

            {
                std::unique_lock<std::mutex> lock(wait_mutex);
                wait_cv.wait(lock, [&]() { return done || g_polling_stop.load(); });
            }

            if (g_polling_stop.load()) {
                task->cancel();
                break;
            }

            if (output != last_output) {
                last_output = output;
                const bool connected = output.find("DevNo=") != std::string::npos;
                const std::string status = connected ? "connected" : "disconnected";
                const std::string info = output;

                w.dispatch([&w, status, info]() {
                    w.eval("window.updateDeviceStatus && window.updateDeviceStatus('" + status + "')");
                    w.eval("window.updateDeviceInfo && window.updateDeviceInfo(" + bindings::js_string_literal(info) + ")");
                });
            }

            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    });
}

} // namespace

#ifdef _WIN32

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)

#else

int main()

#endif

{
    try {
#ifdef _WIN32
        const auto args = win_cli::get_command_line_args();
        if (win_cli::has_flag(args, L"--install-driver")) {
            usb_driver::InstallOptions options;
            options.allow_elevation = false;
            const auto device_name = win_cli::get_flag_value(args, L"--device-name");
            if (!device_name.empty()) {
                options.device_name = win_cli::wide_to_utf8(device_name);
            }
            const auto result = usb_driver::install_libusb_win32(options);
            if (!result.success) {
                logging::write("driver", "Elevated driver install failed: " + result.error_message);
                return 1;
            }
            return 0;
        }
#endif
#ifdef _WIN32
        wchar_t appdata_path[MAX_PATH];
        const DWORD appdata_len = GetEnvironmentVariableW(
            L"LOCALAPPDATA",
            appdata_path,
            static_cast<DWORD>(std::size(appdata_path)));
        if (appdata_len > 0 && appdata_len < std::size(appdata_path)) {
            std::wstring user_data_dir = appdata_path;
            user_data_dir += L"\\HardwareHelper\\WebView2";
            SetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", user_data_dir.c_str());
        }

        SetEnvironmentVariableW(
            L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
            L"--disable-gpu --disable-gpu-compositing --disable-extensions --disable-features=BackForwardCache --no-first-run --disable-background-networking --disable-component-update");
    #elif defined(__APPLE__)
        setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 1);
    #elif defined(__linux__)
        setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 1);
        setenv("WEBKIT_DISABLE_DMABUF_RENDERER", "1", 1);
#endif
        webview::webview w(false, nullptr);

        w.bind("setPollingEnabled", [](const std::string& req) {
            set_device_polling_enabled(bindings::parse_bool_arg(req, true));
            return std::string("true");
        });

        w.bind("logWrite", [](const std::string& req) {
            logging::write(bindings::parse_string_arg(req));
            return std::string("true");
        });

        w.bind("getUsbDriverInfo", [](const std::string&) {
            const auto info = usb_driver::query_driver();
            return std::string("{") +
                "\"found\":" + (info.device_found ? "true" : "false") + "," +
                "\"ok\":" + (info.is_libusb_win32 ? "true" : "false") + "," +
                "\"driver\":" + bindings::js_string_literal(info.driver_name) + "," +
                "\"error\":" + bindings::js_string_literal(info.error_message) +
                "}";
        });

        w.bind("installUsbDriver", [](const std::string& req) {
            usb_driver::InstallOptions options;
            options.device_name = bindings::parse_string_arg(req);
            const auto result = usb_driver::install_libusb_win32(options);
            return std::string("{") +
                "\"success\":" + (result.success ? "true" : "false") + "," +
                "\"error\":" + bindings::js_string_literal(result.error_message) +
                "}";
        });

        w.set_title("Hardware Helper");
        w.set_size(800, 600, WEBVIEW_HINT_NONE);

        w.set_html(R"(
            <!doctype html>
            <html>
            <body style="
                background:#111;
                color:white;
                font-family:sans-serif;
                height:100vh;
                margin:0;
                display:flex;
                align-items:center;
                justify-content:center;
            ">
                <div style="max-width:640px; width:100%; padding:24px;">
                    <h1 style="margin:0 0 16px 0;">Hardware Helper</h1>

                    <div style="display:flex; align-items:center; gap:12px; margin-bottom:12px;">
                        <div id="statusDot" style="width:12px; height:12px; border-radius:50%; background:#a33;"></div>
                        <div id="statusText">disconnected</div>
                        <div id="infoIcon" title="" style="margin-left:auto; width:20px; height:20px; border-radius:50%; border:1px solid #777; display:flex; align-items:center; justify-content:center; font-size:12px; color:#bbb;">i</div>
                    </div>

                    <div id="deviceInfo" style="color:#bbb; white-space:pre-wrap; min-height:24px;">check usb</div>
                    <div id="driverStatus" style="color:#bbb; margin-top:8px; min-height:20px;"></div>

                    <div style="display:flex; gap:8px; margin-top:16px;">
                        <button id="pollingToggle">Pause Polling</button>
                        <button id="testLog">Test Log</button>
                        <button id="installDriver">Install libusb-win32</button>
                    </div>
                </div>

                <script>
                    let pollingEnabled = true;
                    let lastInfo = "";
                    let lastStatus = "disconnected";
                    const driverDeviceName = "Rockchip Bootloader Device";

                    const statusDot = document.getElementById("statusDot");
                    const statusText = document.getElementById("statusText");
                    const deviceInfo = document.getElementById("deviceInfo");
                    const infoIcon = document.getElementById("infoIcon");
                    const pollingToggle = document.getElementById("pollingToggle");
                    const driverStatus = document.getElementById("driverStatus");
                    const installDriver = document.getElementById("installDriver");

                    function render() {
                        const connected = lastStatus === "connected";
                        statusDot.style.background = connected ? "#2fa84f" : "#a33";
                        statusText.textContent = lastStatus;
                        if (connected) {
                            const text = lastInfo.trim() || "device connected";
                            deviceInfo.textContent = text;
                            infoIcon.title = text;
                        } else {
                            deviceInfo.textContent = "check usb";
                            infoIcon.title = "check usb";
                            driverStatus.textContent = "";
                        }
                    }

                    async function refreshDriverInfo() {
                        if (!window.getUsbDriverInfo) {
                            driverStatus.textContent = "driver info unavailable";
                            return;
                        }
                        const raw = await window.getUsbDriverInfo();
                        const info = JSON.parse(raw);
                        if (!info.found) {
                            driverStatus.textContent = info.error || "device not found";
                            return;
                        }
                        if (info.ok) {
                            driverStatus.textContent = "Driver: " + (info.driver || "libusb-win32");
                        } else {
                            driverStatus.textContent = info.error || ("Driver: " + (info.driver || "unknown"));
                        }
                    }

                    window.updateDeviceStatus = (status) => {
                        const changed = status !== lastStatus;
                        lastStatus = status;
                        render();
                        if (changed && status === "connected") {
                            refreshDriverInfo();
                        }
                    };

                    window.updateDeviceInfo = (info) => {
                        lastInfo = info || "";
                        render();
                    };

                    pollingToggle.addEventListener("click", async () => {
                        pollingEnabled = !pollingEnabled;
                        pollingToggle.textContent = pollingEnabled ? "Pause Polling" : "Resume Polling";
                        await window.setPollingEnabled(pollingEnabled);
                    });

                    document.getElementById("testLog").addEventListener("click", async () => {
                        await window.logWrite("[hardware-helper] Test log message");
                    });

                    installDriver.addEventListener("click", async () => {
                        if (!window.installUsbDriver) {
                            driverStatus.textContent = "driver install unavailable";
                            return;
                        }
                        driverStatus.textContent = "Installing driver...";
                        const raw = await window.installUsbDriver(driverDeviceName);
                        const result = JSON.parse(raw);
                        if (!result.success) {
                            driverStatus.textContent = result.error || "driver install failed";
                        } else {
                            driverStatus.textContent = "driver installed";
                        }
                        refreshDriverInfo();
                    });

                    render();
                </script>
            </body>
            </html>
        )");

        start_device_polling(w);

        w.run();
    }
    catch (const webview::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }

    g_polling_stop.store(true);
    if (g_polling_thread.joinable()) {
        g_polling_thread.join();
    }

    return 0;
}