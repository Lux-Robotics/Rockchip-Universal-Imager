#include <saucer/embedded/all.hpp>
#include <saucer/smartview.hpp>
#include "core/rkdeveloptool_runner.h"
#include "core/logging.h"
#include "core/libusb-win32-helper.h"
#include "core/file_dialog.h"
#include "core/loader_map.h"
#include "core/executable_path.h"
#include "core/gpt_probe.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
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
    std::string error;
};

struct UsedSpaceResult {
    bool success = false;
    double used_gb = 0.0;
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

// probe_loader_ready()/query_storage_info() are reached from three different
// threads (the polling loop, flashBootloader's readiness pre-check, and the
// getStorageInfo endpoint fired from JS after a flash completes) and
// g_flash_running alone doesn't close every gap between them - there's a
// TOCTOU window between polling's check-then-act and flashBootloader's
// compare-exchange, and getStorageInfo isn't gated by it at all. Without
// this, two threads can run rfi + regex parsing concurrently, which has been
// observed to segfault (concurrent std::regex_search on the same static
// regex object, inside parse_flash_size_mb). Serialize all of it.
std::mutex g_rkdev_probe_mutex;

void set_device_polling_enabled(bool enabled) {
    g_polling_enabled.store(enabled);
}

// rfi (Read Flash Info) only succeeds once a loader is actually running on
// the device, and fails fast (no hang risk) when it isn't. That makes it a
// reliable, cheap "is this device actually ready for flash/erase" probe -
// far more reliable than parsing "Loader"/"Maskrom" text out of `ld`, which
// we've observed can flip back to Maskrom within ~100ms of a real loader
// session starting, seemingly faster than a subsequent `ld` call can catch.
std::string run_rfi_output() {
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
    return output;
}

bool parse_flash_size_mb(const std::string& rfi_output, unsigned int& out_mb) {
    static const std::regex flash_size_regex(R"(Flash Size:\s*(\d+)\s*MB)", std::regex::icase);
    std::smatch match;
    if (!std::regex_search(rfi_output, match, flash_size_regex) || match.size() < 2) {
        return false;
    }
    try {
        out_mb = static_cast<unsigned int>(std::stoul(match[1].str()));
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_flash_size_sectors(const std::string& rfi_output, std::uint64_t& out_sectors) {
    static const std::regex sectors_regex(R"(Flash Size:\s*(\d+)\s*Sectors)", std::regex::icase);
    std::smatch match;
    if (!std::regex_search(rfi_output, match, sectors_regex) || match.size() < 2) {
        return false;
    }
    try {
        out_sectors = std::stoull(match[1].str());
        return true;
    } catch (...) {
        return false;
    }
}

bool probe_loader_ready() {
    std::lock_guard<std::mutex> lock(g_rkdev_probe_mutex);
    unsigned int unused_mb = 0;
    return parse_flash_size_mb(run_rfi_output(), unused_mb);
}

StorageInfoResult query_storage_info() {
    std::lock_guard<std::mutex> lock(g_rkdev_probe_mutex);
    StorageInfoResult result;
    unsigned int flash_mb = 0;
    if (!parse_flash_size_mb(run_rfi_output(), flash_mb)) {
        // Most commonly: device is still in Maskrom (no loader downloaded
        // yet), which doesn't implement this query.
        result.error = "could not read eMMC size (connect the device first)";
        return result;
    }

    // Reported sizes run a bit under the nominal SKU size (overprovisioning,
    // IDBlock/GPT overhead); round to the nearest GB for display.
    result.emmc_gb = (flash_mb + 512) / 1024;
    result.success = true;
    return result;
}

// Coarse "how much of this eMMC has data" estimate via binary search: probes
// a small chunk at the midpoint of the current [lo, hi) range, and narrows
// toward whichever side has the transition between "has content" and
// "reads as a single uniform byte" (the erased/never-written pattern).
// Assumes a simple two-region layout - real data clustered at the start,
// unused space at the end - which holds for a freshly flashed device but
// isn't a guarantee for a live, fragmented filesystem; that's an accepted
// trade-off for a quick, coarse estimate rather than an exact figure.
UsedSpaceResult calculate_used_space() {
    UsedSpaceResult result;
    std::lock_guard<std::mutex> lock(g_rkdev_probe_mutex);

    std::uint64_t total_sectors = 0;
    if (!parse_flash_size_sectors(run_rfi_output(), total_sectors) || total_sectors == 0) {
        result.error = "could not read eMMC size (connect the device first)";
        return result;
    }

    constexpr std::uint64_t kPrecisionSectors = 204800; // 0.1 GB at 512 B/sector
    constexpr std::uint64_t kProbeSectors = 16;          // 8 KB per probe

    const auto looks_blank = [](const std::vector<std::uint8_t>& buf) {
        if (buf.empty()) {
            return false;
        }
        const auto first = buf.front();
        for (const auto b : buf) {
            if (b != first) {
                return false;
            }
        }
        return true;
    };

    std::uint64_t lo = 0;
    std::uint64_t hi = total_sectors;

    while (hi - lo > kPrecisionSectors) {
        const std::uint64_t mid = lo + (hi - lo) / 2;
        const auto buf = hwhelper::read_sectors(mid, kProbeSectors);
        // A failed read (e.g. probing past the device's real capacity) is
        // treated as "has content" so the search leans conservative.
        const bool blank = buf && looks_blank(*buf);
        if (blank) {
            hi = mid;
        } else {
            lo = mid;
        }
    }

    const std::uint64_t used_sectors = (lo + hi) / 2;
    result.used_gb = static_cast<double>(used_sectors) * 512.0 / (1024.0 * 1024.0 * 1024.0);
    result.success = true;
    return result;
}

void start_device_polling(const std::shared_ptr<saucer::smartview>& view) {
    if (g_polling_thread.joinable()) {
        return;
    }

    std::weak_ptr<saucer::smartview> weak_view = view;
    g_polling_thread = std::thread([weak_view]() {
        std::string last_output;
        std::string last_status;

        while (!g_polling_stop.load()) {
            // A flash/connect/erase task runs its own rkdeveloptool process
            // against the same USB device; running a polling "ld" call at
            // the same time races both processes for the device handle and
            // can hang one of them holding it claimed, leaving the UI stuck
            // (e.g. never observing the post-connect transition to Loader
            // mode). Stand down entirely while a task owns the device.
            if (!g_polling_enabled.load() || g_flash_running.load()) {
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

            const bool present = output.find("DevNo=") != std::string::npos;
            const bool tool_missing = output.find("not found next to the app") != std::string::npos;

            // Maskrom is the initial ROM stage rkdeveloptool enumerates in;
            // it can't do flash/eMMC operations until a loader is pushed to
            // it (download_boot). Whether `ld` prints "Maskrom" or "Loader"
            // is not a reliable signal of that on its own - observed in
            // practice to flip back to "Maskrom" within ~100ms of a loader
            // session actually starting, faster than the next `ld` poll can
            // catch. Probe readiness directly instead: rfi only succeeds
            // once a loader is actually servicing requests, and fails fast
            // otherwise, so it doubles as a safe "has this device already
            // been connected" check for auto-connecting on relaunch without
            // ever needing to retry `db` (which can hang if attempted
            // against a device that's already past Maskrom).
            const bool is_ready = (present && !tool_missing) ? probe_loader_ready() : false;

            std::string status;
            if (tool_missing) {
                status = "tool_missing";
            } else if (present && is_ready) {
                status = "connected";
            } else if (present) {
                status = "detected";
            } else {
                status = "disconnected";
            }

            if (output != last_output || status != last_status) {
                last_output = output;
                last_status = status;
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
    // Persistent-file logging for every rkdeveloptool call/response now
    // happens centrally in RkdevTask::run(); this only mirrors lines into
    // the in-app live-log panel.
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
        logging::write("ui", message);
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

    webview->expose("selectBackupDestination", []() {
        std::string error;
        auto path = pick_img_save_file(error);
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

        // A device that already has a loader running (bootloader flashed in
        // a prior session, no power cycle since) can hang the `db` command
        // if we retry it - Maskrom-only vendor requests don't get a proper
        // response once the device is past that stage. Probe first and, if
        // it's already ready, just confirm that instead of re-flashing.
        bool expected = false;
        if (!g_flash_running.compare_exchange_strong(expected, true)) {
            return StartResult{false, "flash already in progress"};
        }
        const bool already_ready = probe_loader_ready();
        g_flash_running.store(false);

        if (already_ready) {
            if (!start_flash_task(webview, {"rfi"})) {
                return StartResult{false, "flash already in progress"};
            }
            return StartResult{true, ""};
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

    webview->expose("backupEmmc", [webview](const std::string& dest_path) {
        if (dest_path.empty()) {
            return StartResult{false, "no destination selected"};
        }

        bool expected = false;
        if (!g_flash_running.compare_exchange_strong(expected, true)) {
            return StartResult{false, "flash already in progress"};
        }

        std::uint64_t main_sectors = 0;
        std::uint64_t total_sectors = 0;
        {
            std::lock_guard<std::mutex> lock(g_rkdev_probe_mutex);
            if (const auto gpt = hwhelper::read_gpt_info()) {
                main_sectors = gpt->last_used_lba + 1;
            }
            parse_flash_size_sectors(run_rfi_output(), total_sectors);
        }
        g_flash_running.store(false);

        if (main_sectors == 0) {
            if (total_sectors == 0) {
                return StartResult{false, "could not determine eMMC size (connect the device first)"};
            }
            // No valid GPT found. Before falling back to a full dump, check
            // whether there's any reason to - a device that's never been
            // flashed (or was just erased) has nothing worth backing up.
            bool appears_blank = false;
            {
                std::lock_guard<std::mutex> lock(g_rkdev_probe_mutex);
                appears_blank = hwhelper::probe_emmc_appears_blank(total_sectors);
            }
            if (appears_blank) {
                return StartResult{false, "eMMC appears blank (no partition table found) - nothing to back up"};
            }
            // Has data, just not a GPT layout we recognize; dump everything
            // rather than silently drop data we can't account for.
            main_sectors = total_sectors;
        }

        if (!start_flash_task(webview, {"rl", "0", std::to_string(main_sectors), dest_path})) {
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

    webview->expose("calculateUsedSpace", []() {
        return calculate_used_space();
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