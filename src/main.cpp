#include "webview/webview.h"
#include "core/rkdeveloptool_runner.h"
#include "core/logging.h"
#include "core/libusb-win32-helper.h"
#include "core/webview_bindings.h"
#include "core/file_dialog.h"
#include "core/loader_map.h"
#include "ui_embed.h"

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
std::atomic<bool> g_ui_ready{false};
std::atomic<bool> g_flash_running{false};
std::atomic<unsigned int> g_last_detected_vid{0};
std::atomic<unsigned int> g_last_detected_pid{0};
std::shared_ptr<rkdev::RkdevTask> g_flash_task;
std::mutex g_flash_mutex;

void replace_all(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return;
    }
    size_t start = 0;
    while ((start = text.find(from, start)) != std::string::npos) {
        text.replace(start, from.size(), to);
        start += to.size();
    }
}

std::string build_ui_html() {
    std::string html = ui::kIndexHtml ? ui::kIndexHtml : "";
    replace_all(html, "{{INLINE_CSS}}", ui::kAppCss ? ui::kAppCss : "");
    replace_all(html, "{{INLINE_JS}}", ui::kAppJs ? ui::kAppJs : "");
    return html;
}

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

        w.bind("uiReady", [&w](const std::string&) {
            bool expected = false;
            if (g_ui_ready.compare_exchange_strong(expected, true)) {
                start_device_polling(w);
            }
            return std::string("true");
        });

        w.bind("getLogContents", [](const std::string&) {
            const std::string text = logging::read_all();
            return std::string("{") +
                "\"ok\":true," +
                "\"text\":" + bindings::js_string_literal(text) +
                "}";
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

        const std::string ui_html = build_ui_html();
        if (ui_html.empty()) {
            w.set_html("<html><body style=\"background:#111;color:#bbb;font-family:sans-serif;\">Missing embedded UI</body></html>");
        } else {
            w.set_html(ui_html);
        }

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