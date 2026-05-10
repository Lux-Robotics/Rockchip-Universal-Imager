#include "webview/webview.h"
#include "rkdeveloptool_runner.h"
#include "logging.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
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

                auto js_escape = [](const std::string& input) {
                    std::string out;
                    out.reserve(input.size() + 2);
                    out.push_back('"');
                    for (char ch : input) {
                        switch (ch) {
                        case '\\': out += "\\\\"; break;
                        case '"': out += "\\\""; break;
                        case '\n': out += "\\n"; break;
                        case '\r': out += "\\r"; break;
                        case '\t': out += "\\t"; break;
                        default: out.push_back(ch); break;
                        }
                    }
                    out.push_back('"');
                    return out;
                };

                w.dispatch([&w, status, info, js_escape]() {
                    w.eval("window.updateDeviceStatus && window.updateDeviceStatus('" + status + "')");
                    w.eval("window.updateDeviceInfo && window.updateDeviceInfo(" + js_escape(info) + ")");
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
        webview::webview w(false, nullptr);

        w.bind("setPollingEnabled", [](bool enabled) {
            set_device_polling_enabled(enabled);
            return true;
        });

        w.bind("logWrite", [](const std::string& message) {
            logging::write(message);
            return true;
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

                    <div style="display:flex; gap:8px; margin-top:16px;">
                        <button id="pollingToggle">Pause Polling</button>
                        <button id="testLog">Test Log</button>
                    </div>
                </div>

                <script>
                    let pollingEnabled = true;
                    let lastInfo = "";
                    let lastStatus = "disconnected";

                    const statusDot = document.getElementById("statusDot");
                    const statusText = document.getElementById("statusText");
                    const deviceInfo = document.getElementById("deviceInfo");
                    const infoIcon = document.getElementById("infoIcon");
                    const pollingToggle = document.getElementById("pollingToggle");

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
                        }
                    }

                    window.updateDeviceStatus = (status) => {
                        lastStatus = status;
                        render();
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