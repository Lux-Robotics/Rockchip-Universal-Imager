#include "webview/webview.h"
#include "rkdeveloptool_runner.h"
#include "logging.h"
#include "libusb-win32-helper.h"
#include "webview_bindings.h"
#include "file_dialog.h"
#include "loader_map.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#endif

namespace {

std::atomic<bool> g_polling_enabled{true};
std::atomic<bool> g_polling_stop{false};
std::thread g_polling_thread;
std::atomic<bool> g_driver_install_running{false};
std::atomic<bool> g_webview_alive{false};
std::atomic<bool> g_flash_running{false};
std::atomic<unsigned int> g_last_detected_vid{0};
std::atomic<unsigned int> g_last_detected_pid{0};
std::shared_ptr<rkdev::RkdevTask> g_flash_task;
std::mutex g_flash_mutex;

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
                    static const std::regex vid_regex("VID[:=]0x?([0-9A-Fa-f]{4})");
                    static const std::regex pid_regex("PID[:=]0x?([0-9A-Fa-f]{4})");
                    std::smatch match;
                    if (std::regex_search(line, match, vid_regex) && match.size() >= 2) {
                        const auto hex = match[1].str();
                        unsigned int vid = 0;
                        try {
                            vid = static_cast<unsigned int>(std::stoul(hex, nullptr, 16));
                            g_last_detected_vid.store(vid);
                        } catch (...) {
                        }
                    }
                    if (std::regex_search(line, match, pid_regex) && match.size() >= 2) {
                        const auto hex = match[1].str();
                        unsigned int pid = 0;
                        try {
                            pid = static_cast<unsigned int>(std::stoul(hex, nullptr, 16));
                            g_last_detected_pid.store(pid);
                        } catch (...) {
                        }
                    }
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

void append_live_log(webview::webview& w, const std::string& line) {
    logging::write("flash", line);
    if (!g_webview_alive.load()) {
        return;
    }
    const std::string payload = bindings::js_string_literal(line);
    w.dispatch([&w, payload]() {
        w.eval("window.appendLiveLog && window.appendLiveLog(" + payload + ")");
    });
}

void update_flash_progress(webview::webview& w, int percent) {
    if (!g_webview_alive.load()) {
        return;
    }
    const int clamped = std::clamp(percent, 0, 100);
    w.dispatch([&w, clamped]() {
        w.eval("window.updateFlashProgress && window.updateFlashProgress(" + std::to_string(clamped) + ")");
    });
}

std::optional<std::string> loader_path_for_vid(unsigned short vid, unsigned short pid, std::string& error) {
    for (size_t i = 0; i < kLoaderMapSize; ++i) {
        if (kLoaderMap[i].vid == vid && kLoaderMap[i].pid == pid) {
            const std::filesystem::path path = std::filesystem::current_path() / "loaders" / kLoaderMap[i].filename;
            if (!std::filesystem::exists(path)) {
                error = "loader file not found: " + path.string();
                return std::nullopt;
            }
            return path.string();
        }
    }

    char buf[7];
    std::snprintf(buf, sizeof(buf), "%04X", vid);
    char pid_buf[7];
    std::snprintf(pid_buf, sizeof(pid_buf), "%04X", pid);
    error = std::string("no loader mapping for VID 0x") + buf + " PID 0x" + pid_buf;
    return std::nullopt;
}

bool start_flash_task(webview::webview& w,
                      const std::vector<std::string>& args,
                      bool parse_progress) {
    bool expected = false;
    if (!g_flash_running.compare_exchange_strong(expected, true)) {
        return false;
    }

    update_flash_progress(w, 0);
    append_live_log(w, "Starting rkdeveloptool " + args.front());

    auto on_line = [&](const std::string& line) {
        append_live_log(w, line);
        if (parse_progress) {
            static const std::regex progress_regex("([0-9]{1,3})%");
            std::smatch match;
            if (std::regex_search(line, match, progress_regex) && match.size() >= 2) {
                try {
                    const int percent = std::stoi(match[1].str());
                    update_flash_progress(w, percent);
                } catch (...) {
                }
            }
        }
    };

    auto on_exit = [&](const rkdev::ProcessResult& result) {
        g_flash_running.store(false);
        if (!g_webview_alive.load()) {
            return;
        }
        if (result.exit_code == 0 && result.error_message.empty() && !result.was_cancelled) {
            update_flash_progress(w, 100);
        }
        const bool success = (result.exit_code == 0 && result.error_message.empty() && !result.was_cancelled);
        const std::string error_text = result.error_message.empty() && !success
            ? "rkdeveloptool failed with exit code " + std::to_string(result.exit_code)
            : result.error_message;
        const std::string payload = std::string("{") +
            "\"success\":" + (success ? "true" : "false") + "," +
            "\"error\":" + bindings::js_string_literal(error_text) +
            "}";
        w.dispatch([&w, payload]() {
            w.eval("window.onFlashComplete && window.onFlashComplete(" + payload + ")");
        });
    };

    auto task = rkdev::start_rkdeveloptool(args, on_line, on_exit);
    {
        std::lock_guard<std::mutex> lock(g_flash_mutex);
        g_flash_task = task;
    }
    return true;
}

bool start_driver_install(webview::webview& w, const std::string& device_name) {
    bool expected = false;
    if (!g_driver_install_running.compare_exchange_strong(expected, true)) {
        return false;
    }

    webview::webview* wptr = &w;
    std::thread([wptr, device_name]() {
        usb_driver::InstallOptions options;
        options.device_name = device_name;
        const auto result = usb_driver::install_libusb_win32(options);
        g_driver_install_running.store(false);

        if (!g_webview_alive.load()) {
            return;
        }

        const std::string payload = std::string("{") +
            "\"success\":" + (result.success ? "true" : "false") + "," +
            "\"error\":" + bindings::js_string_literal(result.error_message) +
            "}";
        wptr->dispatch([wptr, payload]() {
            wptr->eval("window.onDriverInstallComplete && window.onDriverInstallComplete(" + payload + ")");
        });
    }).detach();

    return true;
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
        int exit_code = 0;
        if (win_driver::try_handle_driver_install_cli(exit_code)) {
            return exit_code;
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
        g_webview_alive.store(true);

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

        w.bind("installUsbDriver", [&w](const std::string& req) {
            const std::string device_name = bindings::parse_string_arg(req);
            if (!start_driver_install(w, device_name)) {
                return std::string("{") +
                    "\"started\":false," +
                    "\"error\":" + bindings::js_string_literal("driver install already in progress") +
                    "}";
            }
            return std::string("{") +
                "\"started\":true" +
                "}";
        });

        w.bind("selectImageFile", [](const std::string&) {
            std::string error;
            auto path = pick_img_file(error);
            if (!path) {
                return std::string("{") +
                    "\"success\":false," +
                    "\"error\":" + bindings::js_string_literal(error.empty() ? "file picker canceled" : error) +
                    "}";
            }
            return std::string("{") +
                "\"success\":true," +
                "\"path\":" + bindings::js_string_literal(*path) +
                "}";
        });

        w.bind("flashBootloader", [&w](const std::string&) {
            const unsigned int vid = g_last_detected_vid.load();
            const unsigned int pid = g_last_detected_pid.load();
            if (vid == 0 || pid == 0) {
                return std::string("{") +
                    "\"started\":false," +
                    "\"error\":" + bindings::js_string_literal("device VID/PID not detected") +
                    "}";
            }

            std::string error;
            auto loader = loader_path_for_vid(static_cast<unsigned short>(vid), static_cast<unsigned short>(pid), error);
            if (!loader) {
                return std::string("{") +
                    "\"started\":false," +
                    "\"error\":" + bindings::js_string_literal(error) +
                    "}";
            }

            if (!start_flash_task(w, {"db", *loader}, false)) {
                return std::string("{") +
                    "\"started\":false," +
                    "\"error\":" + bindings::js_string_literal("flash already in progress") +
                    "}";
            }

            return std::string("{") +
                "\"started\":true" +
                "}";
        });

        w.bind("flashImage", [&w](const std::string& req) {
            const std::string image_path = bindings::parse_string_arg(req);
            if (image_path.empty()) {
                return std::string("{") +
                    "\"started\":false," +
                    "\"error\":" + bindings::js_string_literal("no .img file selected") +
                    "}";
            }

            const std::filesystem::path path(image_path);
            if (path.extension() != ".img") {
                return std::string("{") +
                    "\"started\":false," +
                    "\"error\":" + bindings::js_string_literal("selected file is not a .img") +
                    "}";
            }
            if (!std::filesystem::exists(path)) {
                return std::string("{") +
                    "\"started\":false," +
                    "\"error\":" + bindings::js_string_literal("selected file does not exist") +
                    "}";
            }

            if (!start_flash_task(w, {"wl", "0", image_path}, true)) {
                return std::string("{") +
                    "\"started\":false," +
                    "\"error\":" + bindings::js_string_literal("flash already in progress") +
                    "}";
            }

            return std::string("{") +
                "\"started\":true" +
                "}";
        });

        w.set_title("Hardware Helper");
        w.set_size(800, 600, WEBVIEW_HINT_NONE);

        w.set_html(R"HTML(
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
                    <div id="flashStatus" style="color:#bbb; margin-top:8px; min-height:20px;"></div>

                    <div style="display:flex; gap:8px; margin-top:16px; flex-wrap:wrap;">
                        <button id="pollingToggle">Pause Polling</button>
                        <button id="testLog">Test Log</button>
                        <button id="toggleLog">Show Log</button>
                        <button id="installDriver">Install libusb-win32</button>
                    </div>

                    <div style="display:flex; gap:8px; margin-top:16px; flex-wrap:wrap;">
                        <button id="selectImage">Select .img</button>
                        <button id="flashBootloader">Flash Bootloader</button>
                        <button id="flashImage">Flash Image</button>
                    </div>

                    <div id="selectedImage" style="color:#888; margin-top:8px; word-break:break-all;">No image selected</div>
                    <div style="margin-top:8px;">
                        <progress id="flashProgress" max="100" value="0" style="width:100%;"></progress>
                    </div>

                    <div id="logPanel" style="display:none; margin-top:16px;">
                        <div style="display:flex; gap:8px; margin-bottom:8px;">
                            <button id="copyLog">Copy Log</button>
                            <button id="clearLog">Clear Log</button>
                        </div>
                        <textarea id="liveLog" readonly style="width:100%; height:160px; background:#0b0b0b; color:#9bd; border:1px solid #333; padding:8px;"></textarea>
                    </div>
                </div>

                <script>
                    let pollingEnabled = true;
                    let lastInfo = "";
                    let lastStatus = "disconnected";
                    let driverInstallRunning = false;
                    let flashRunning = false;
                    let selectedImagePath = "";
                    let logVisible = false;
                    const driverDeviceName = "Rockchip Bootloader Device";

                    const statusDot = document.getElementById("statusDot");
                    const statusText = document.getElementById("statusText");
                    const deviceInfo = document.getElementById("deviceInfo");
                    const infoIcon = document.getElementById("infoIcon");
                    const pollingToggle = document.getElementById("pollingToggle");
                    const driverStatus = document.getElementById("driverStatus");
                    const installDriver = document.getElementById("installDriver");
                    const flashStatus = document.getElementById("flashStatus");
                    const flashProgress = document.getElementById("flashProgress");
                    const selectImage = document.getElementById("selectImage");
                    const flashBootloader = document.getElementById("flashBootloader");
                    const flashImage = document.getElementById("flashImage");
                    const selectedImage = document.getElementById("selectedImage");
                    const toggleLog = document.getElementById("toggleLog");
                    const logPanel = document.getElementById("logPanel");
                    const liveLog = document.getElementById("liveLog");
                    const copyLog = document.getElementById("copyLog");
                    const clearLog = document.getElementById("clearLog");

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
                            if (!driverInstallRunning) {
                                driverStatus.textContent = "";
                            }
                        }
                    }

                    function setDriverInstallRunning(running) {
                        driverInstallRunning = running;
                        installDriver.disabled = running;
                        if (running) {
                            driverStatus.textContent = "Installing driver... (this may take a while)";
                        }
                    }

                    function setFlashRunning(running) {
                        flashRunning = running;
                        selectImage.disabled = running;
                        flashBootloader.disabled = running;
                        flashImage.disabled = running;
                        if (running) {
                            flashStatus.textContent = "Flashing...";
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

                    toggleLog.addEventListener("click", () => {
                        logVisible = !logVisible;
                        logPanel.style.display = logVisible ? "block" : "none";
                        toggleLog.textContent = logVisible ? "Hide Log" : "Show Log";
                    });

                    copyLog.addEventListener("click", async () => {
                        const text = liveLog.value || "";
                        if (navigator.clipboard && navigator.clipboard.writeText) {
                            await navigator.clipboard.writeText(text);
                        } else {
                            liveLog.select();
                            document.execCommand("copy");
                            liveLog.setSelectionRange(0, 0);
                        }
                    });

                    clearLog.addEventListener("click", () => {
                        liveLog.value = "";
                    });

                    document.getElementById("testLog").addEventListener("click", async () => {
                        await window.logWrite("[hardware-helper] Test log message");
                    });

                    selectImage.addEventListener("click", async () => {
                        if (!window.selectImageFile) {
                            flashStatus.textContent = "file picker unavailable";
                            return;
                        }
                        const raw = await window.selectImageFile();
                        const result = JSON.parse(raw);
                        if (!result.success) {
                            flashStatus.textContent = result.error || "file picker canceled";
                            return;
                        }
                        selectedImagePath = result.path;
                        selectedImage.textContent = result.path;
                        flashStatus.textContent = "image selected";
                    });

                    flashBootloader.addEventListener("click", async () => {
                        if (!window.flashBootloader) {
                            flashStatus.textContent = "flash unavailable";
                            return;
                        }
                        if (flashRunning) {
                            return;
                        }
                        setFlashRunning(true);
                        flashProgress.value = 0;
                        const raw = await window.flashBootloader();
                        const result = JSON.parse(raw);
                        if (!result.started) {
                            setFlashRunning(false);
                            flashStatus.textContent = result.error || "flash failed";
                        }
                    });

                    flashImage.addEventListener("click", async () => {
                        if (!window.flashImage) {
                            flashStatus.textContent = "flash unavailable";
                            return;
                        }
                        if (flashRunning) {
                            return;
                        }
                        setFlashRunning(true);
                        flashProgress.value = 0;
                        const raw = await window.flashImage(selectedImagePath);
                        const result = JSON.parse(raw);
                        if (!result.started) {
                            setFlashRunning(false);
                            flashStatus.textContent = result.error || "flash failed";
                        }
                    });

                    installDriver.addEventListener("click", async () => {
                        if (!window.installUsbDriver) {
                            driverStatus.textContent = "driver install unavailable";
                            return;
                        }
                        if (driverInstallRunning) {
                            return;
                        }
                        setDriverInstallRunning(true);
                        const raw = await window.installUsbDriver(driverDeviceName);
                        const result = JSON.parse(raw);
                        if (!result.started) {
                            setDriverInstallRunning(false);
                            driverStatus.textContent = result.error || "driver install already in progress";
                        }
                    });

                    window.onDriverInstallComplete = (result) => {
                        setDriverInstallRunning(false);
                        if (!result || !result.success) {
                            driverStatus.textContent = (result && result.error) || "driver install failed";
                        } else {
                            driverStatus.textContent = "driver installed";
                        }
                        refreshDriverInfo();
                    };

                    window.onFlashComplete = (result) => {
                        setFlashRunning(false);
                        if (!result || !result.success) {
                            flashStatus.textContent = (result && result.error) || "flash failed";
                        } else {
                            flashStatus.textContent = "flash completed";
                        }
                    };

                    window.updateFlashProgress = (percent) => {
                        const value = Math.max(0, Math.min(100, percent || 0));
                        flashProgress.value = value;
                    };

                    window.appendLiveLog = (line) => {
                        if (!line) {
                            return;
                        }
                        liveLog.value += line + "\n";
                        liveLog.scrollTop = liveLog.scrollHeight;
                    };

                    render();
                </script>
            </body>
            </html>
        )HTML");

        start_device_polling(w);

        w.run();
        g_webview_alive.store(false);
    }
    catch (const webview::exception& e) {
        std::cerr << e.what() << '\n';
        g_webview_alive.store(false);
        return 1;
    }

    g_polling_stop.store(true);
    if (g_polling_thread.joinable()) {
        g_polling_thread.join();
    }

    return 0;
}