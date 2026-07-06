#include <saucer/embedded/all.hpp>
#include <saucer/smartview.hpp>
#include "core/rkdeveloptool_runner.h"
#include "core/logging.h"
#include "core/libusb-win32-helper.h"
#include "core/file_dialog.h"
#include "core/loader_map.h"
#include "core/executable_path.h"
#include "core/emmc_ram_map.h"

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


struct StartResult {
    bool started = false;
    std::string error;
};

struct OperationResult {
    bool success = false;
    bool cancelled = false;
    std::string error;
};

struct StorageInfoResult {
    bool success = false;
    unsigned int emmc_gb = 0;
    unsigned int ram_gb = 0;
    std::string error;
};

struct FilePickResult {
    bool success = false;
    std::string path;
    std::string error;
};

struct DriverInfoResult {
    bool found = false;
    bool ok = false;
    std::string driver;
    std::string error;
};

struct LogContentsResult {
    bool ok = false;
    std::string text;
};

namespace {

std::atomic<bool> g_polling_enabled{true};
std::atomic<bool> g_polling_stop{false};
std::thread g_polling_thread;
std::atomic<bool> g_driver_install_running{false};
std::atomic<bool> g_webview_alive{false};
std::atomic<bool> g_ui_ready{false};
std::atomic<bool> g_flash_running{false};
std::atomic<unsigned int> g_last_detected_vid{0};
std::atomic<unsigned int> g_last_detected_pid{0};
std::shared_ptr<rkdev::RkdevTask> g_flash_task;
std::mutex g_flash_mutex;

void set_device_polling_enabled(bool enabled) {
    g_polling_enabled.store(enabled);
}

void start_device_polling(const std::shared_ptr<saucer::smartview>& view) {
    if (g_polling_thread.joinable()) {
        return;
    }

    std::weak_ptr<saucer::smartview> weak_view = view;
    g_polling_thread = std::thread([weak_view]() {
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
                    static const std::regex vid_regex("VID[:=]0x?([0-9A-Fa-f]{4})", std::regex::icase);
                    static const std::regex pid_regex("PID[:=]0x?([0-9A-Fa-f]{4})", std::regex::icase);
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
                const bool present = output.find("DevNo=") != std::string::npos;
                const bool tool_missing = output.find("not found next to the app") != std::string::npos;
                // Maskrom is the initial ROM stage rkdeveloptool enumerates in;
                // it can't do flash/eMMC operations until a loader is pushed
                // to it (download_boot), after which the device re-enumerates
                // as "Loader" mode. Surface these as distinct states so the UI
                // can gate flashing/erase/storage-info behind an explicit
                // "Connect" step rather than pretending Maskrom == ready.
                const bool is_loader = output.find("Loader") != std::string::npos;
                std::string status;
                if (tool_missing) {
                    status = "tool_missing";
                } else if (present && is_loader) {
                    status = "connected";
                } else if (present) {
                    status = "detected";
                } else {
                    status = "disconnected";
                }
                const std::string info = output;

                if (auto view = weak_view.lock()) {
                    view->execute("window.updateDeviceStatus && window.updateDeviceStatus({})", status);
                    view->execute("window.updateDeviceInfo && window.updateDeviceInfo({})", info);
                }
            }

            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    });
}

void append_live_log(const std::shared_ptr<saucer::smartview>& view, const std::string& line) {
    logging::write("flash", line);
    if (!g_webview_alive.load()) {
        return;
    }
    view->execute("window.appendLiveLog && window.appendLiveLog({})", line);
}

void update_flash_progress(const std::shared_ptr<saucer::smartview>& view, int percent) {
    if (!g_webview_alive.load()) {
        return;
    }
    const int clamped = std::clamp(percent, 0, 100);
    view->execute("window.updateFlashProgress && window.updateFlashProgress({})", clamped);
}

std::optional<std::string> loader_path_for_vid(unsigned short vid, unsigned short pid, std::string& error) {
    for (size_t i = 0; i < kLoaderMapSize; ++i) {
        if (kLoaderMap[i].vid == vid && kLoaderMap[i].pid == pid) {
            const std::filesystem::path path = hwhelper::executable_dir() / "loader_binaries" / kLoaderMap[i].filename;
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

// rkdeveloptool reports progress in two different shapes depending on the
// operation: a direct "(NN%)" for Write LBA, or a "total N, current M" pair
// for erase (no ready-made percentage). Handle both from one call site.
std::optional<int> parse_progress_percent(const std::string& line) {
    static const std::regex percent_regex("([0-9]{1,3})%");
    static const std::regex ratio_regex(R"(total\s+(\d+)K?,\s*current\s+(\d+)K?)", std::regex::icase);
    std::smatch match;
    if (std::regex_search(line, match, percent_regex) && match.size() >= 2) {
        try {
            return std::clamp(std::stoi(match[1].str()), 0, 100);
        } catch (...) {
            return std::nullopt;
        }
    }
    if (std::regex_search(line, match, ratio_regex) && match.size() >= 3) {
        try {
            const long long total = std::stoll(match[1].str());
            const long long current = std::stoll(match[2].str());
            if (total > 0) {
                return std::clamp(static_cast<int>(current * 100 / total), 0, 100);
            }
        } catch (...) {
        }
    }
    return std::nullopt;
}

bool start_flash_task(const std::shared_ptr<saucer::smartview>& view,
                      const std::vector<std::string>& args) {
    bool expected = false;
    if (!g_flash_running.compare_exchange_strong(expected, true)) {
        return false;
    }

    update_flash_progress(view, 0);
    append_live_log(view, "Starting rkdeveloptool " + args.front());

    auto on_line = [&](const std::string& line) {
        append_live_log(view, line);
        if (const auto percent = parse_progress_percent(line)) {
            update_flash_progress(view, *percent);
        }
    };

    auto on_exit = [&](const rkdev::ProcessResult& result) {
        g_flash_running.store(false);
        if (!g_webview_alive.load()) {
            return;
        }
        const bool cancelled = result.was_cancelled;
        const bool success = (result.exit_code == 0 && result.error_message.empty() && !cancelled);
        if (success) {
            update_flash_progress(view, 100);
        }
        std::string error_text;
        if (!success && !cancelled) {
            error_text = result.error_message.empty()
                ? "rkdeveloptool failed with exit code " + std::to_string(result.exit_code)
                : result.error_message;
        }
        const OperationResult payload{success, cancelled, error_text};
        view->execute("window.onFlashComplete && window.onFlashComplete({})", payload);
    };

    auto task = rkdev::start_rkdeveloptool(args, on_line, on_exit);
    {
        std::lock_guard<std::mutex> lock(g_flash_mutex);
        g_flash_task = task;
    }
    return true;
}

bool cancel_flash_task() {
    std::lock_guard<std::mutex> lock(g_flash_mutex);
    if (!g_flash_task || !g_flash_running.load()) {
        return false;
    }
    g_flash_task->cancel();
    return true;
}

StorageInfoResult query_storage_info() {
    StorageInfoResult result;
    std::string output;
    std::mutex wait_mutex;
    std::condition_variable wait_cv;
    bool done = false;

    auto task = rkdev::start_rkdeveloptool(
        {"rfi"},
        [&](const std::string& line) {
            output += line + "\n";
        },
        [&](const rkdev::ProcessResult& proc_result) {
            if (!proc_result.error_message.empty()) {
                output += "Error: " + proc_result.error_message + "\n";
            }
            {
                std::lock_guard<std::mutex> lock(wait_mutex);
                done = true;
            }
            wait_cv.notify_one();
        });

    {
        std::unique_lock<std::mutex> lock(wait_mutex);
        wait_cv.wait(lock, [&]() { return done; });
    }

    static const std::regex flash_size_regex(R"(Flash Size:\s*(\d+)\s*MB)", std::regex::icase);
    std::smatch match;
    if (!std::regex_search(output, match, flash_size_regex) || match.size() < 2) {
        // Most commonly: device is still in Maskrom (no loader downloaded
        // yet), which doesn't implement this query.
        result.error = "could not read eMMC size (connect the device first)";
        return result;
    }

    try {
        const unsigned int flash_mb = static_cast<unsigned int>(std::stoul(match[1].str()));
        // Reported sizes run a bit under the nominal SKU size (overprovisioning,
        // IDBlock/GPT overhead); round to nearest GB, then snap to the closest
        // known SKU rather than requiring an exact match.
        const unsigned int flash_gb = (flash_mb + 512) / 1024;
        result.emmc_gb = flash_gb;
        result.ram_gb = hwhelper::ram_gb_for_emmc(flash_gb);
        result.success = true;
    } catch (...) {
        result.error = "failed to parse eMMC size";
    }

    return result;
}

bool start_driver_install(const std::shared_ptr<saucer::smartview>& view, const std::string& device_name) {
    bool expected = false;
    if (!g_driver_install_running.compare_exchange_strong(expected, true)) {
        return false;
    }

    std::weak_ptr<saucer::smartview> weak_view = view;
    std::thread([weak_view, device_name]() {
        usb_driver::InstallOptions options;
        options.device_name = device_name;
        const auto result = usb_driver::install_libusb_win32(options);
        g_driver_install_running.store(false);

        if (!g_webview_alive.load()) {
            return;
        }
        if (auto view = weak_view.lock()) {
            const OperationResult payload{result.success, false, result.error_message};
            view->execute("window.onDriverInstallComplete && window.onDriverInstallComplete({})", payload);
        }
    }).detach();

    return true;
}

coco::stray run_app(saucer::application* app) {
    auto window = saucer::window::create(app).value();
    auto webview = std::make_shared<saucer::smartview>(saucer::smartview::create({.window = window}).value());

    g_webview_alive.store(true);

    webview->expose("setPollingEnabled", [](bool enabled) {
        set_device_polling_enabled(enabled);
        return true;
    });

    webview->expose("logWrite", [](const std::string& message) {
        logging::write(message);
        return true;
    });

    webview->expose("uiReady", [webview]() {
        bool expected = false;
        if (g_ui_ready.compare_exchange_strong(expected, true)) {
            start_device_polling(webview);
        }
        return true;
    });

    webview->expose("getLogContents", []() {
        return LogContentsResult{true, logging::read_all()};
    });

    // saucer's window.saucer.exposed is a JS Proxy that returns a callable
    // stub for *any* property name regardless of whether it was actually
    // registered here, so the frontend can't detect Windows-only endpoints
    // (installUsbDriver/getUsbDriverInfo) by truthiness-checking them. Expose
    // an explicit platform flag instead.
    webview->expose("isWindowsPlatform", []() {
#ifdef _WIN32
        return true;
#else
        return false;
#endif
    });

#ifdef _WIN32
    webview->expose("getUsbDriverInfo", []() {
        const auto info = usb_driver::query_driver();
        return DriverInfoResult{info.device_found, info.is_libusb_win32, info.driver_name, info.error_message};
    });

    webview->expose("installUsbDriver", [webview](const std::string& device_name) {
        if (!start_driver_install(webview, device_name)) {
            return StartResult{false, "driver install already in progress"};
        }
        return StartResult{true, ""};
    });
#endif

    webview->expose("selectImageFile", []() {
        std::string error;
        auto path = pick_img_file(error);
        if (!path) {
            return FilePickResult{false, std::string(), error.empty() ? "file picker canceled" : error};
        }
        return FilePickResult{true, *path, ""};
    });

    webview->expose("flashBootloader", [webview]() {
        const unsigned int vid = g_last_detected_vid.load();
        const unsigned int pid = g_last_detected_pid.load();
        if (vid == 0 || pid == 0) {
            return StartResult{false, "device VID/PID not detected"};
        }

        std::string error;
        auto loader = loader_path_for_vid(static_cast<unsigned short>(vid), static_cast<unsigned short>(pid), error);
        if (!loader) {
            return StartResult{false, error};
        }

        if (!start_flash_task(webview, {"db", *loader})) {
            return StartResult{false, "flash already in progress"};
        }

        return StartResult{true, ""};
    });

    webview->expose("flashImage", [webview](const std::string& image_path) {
        if (image_path.empty()) {
            return StartResult{false, "no .img file selected"};
        }

        const std::filesystem::path path(image_path);
        if (path.extension() != ".img") {
            return StartResult{false, "selected file is not a .img"};
        }
        if (!std::filesystem::exists(path)) {
            return StartResult{false, "selected file does not exist"};
        }

        if (!start_flash_task(webview, {"wl", "0", image_path})) {
            return StartResult{false, "flash already in progress"};
        }

        return StartResult{true, ""};
    });

    webview->expose("eraseEmmc", [webview]() {
        if (!start_flash_task(webview, {"ef"})) {
            return StartResult{false, "flash already in progress"};
        }
        return StartResult{true, ""};
    });

    webview->expose("cancelFlash", []() {
        if (!cancel_flash_task()) {
            return StartResult{false, "no flash in progress"};
        }
        return StartResult{true, ""};
    });

    webview->expose("getStorageInfo", []() {
        return query_storage_info();
    });

    window->set_title("Hardware Helper");
    window->set_size({.w = 800, .h = 600});

    webview->embed(saucer::embedded::all());
    webview->serve("/index.html");
    window->show();

    co_await app->finish();

    g_webview_alive.store(false);
    g_polling_stop.store(true);
    if (g_polling_thread.joinable()) {
        g_polling_thread.join();
    }
}

} // namespace

int run_application() {
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

#if defined(HWHELPER_DISABLE_GPU)
    SetEnvironmentVariableW(
        L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
        L"--disable-gpu --disable-gpu-compositing --disable-extensions --disable-features=BackForwardCache --no-first-run --disable-background-networking --disable-component-update");
#endif
#elif defined(__APPLE__)
#if defined(HWHELPER_DISABLE_GPU)
    setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 1);
#endif
#elif defined(__linux__)
#if defined(HWHELPER_DISABLE_GPU)
    setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 1);
    setenv("WEBKIT_DISABLE_DMABUF_RENDERER", "1", 1);
#endif
#endif

    auto app = saucer::application::create({.id = "hardware-helper"});
    return app->run(run_app);
}

#ifdef _WIN32

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return run_application();
}

#else

int main() {
    return run_application();
}

#endif