#include "core/usb_monitor.h"

#include <atomic>
#include <thread>

#include <libusb.h>

#include "core/logging.h"

namespace hwhelper {
namespace {

// All Rockchip parts enumerate under this vendor ID in Maskrom and loader
// (rockusb) modes; the PID identifies the SoC (see loader_map.h).
constexpr unsigned short kRockchipVid = 0x2207;

libusb_context* g_ctx = nullptr;
libusb_hotplug_callback_handle g_cb_handle = 0;
std::thread g_event_thread;
std::atomic<bool> g_stop{false};
UsbChangeCallback g_on_change;

int LIBUSB_CALL hotplug_cb(libusb_context* /*ctx*/, libusb_device* dev,
                           libusb_hotplug_event event, void* /*user_data*/) {
    // Reads from the cached descriptor - no device open, no interface claim,
    // so this never contends with rkdeveloptool's exclusive claim.
    libusb_device_descriptor desc{};
    if (libusb_get_device_descriptor(dev, &desc) != LIBUSB_SUCCESS) {
        return 0; // stay registered
    }
    const bool present = (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED);
    if (g_on_change) {
        g_on_change(present, desc.idVendor, desc.idProduct);
    }
    return 0; // returning non-zero would deregister the callback
}

} // namespace

bool start_usb_monitor(UsbChangeCallback on_change) {
    if (g_ctx != nullptr) {
        return true; // already running
    }

    if (libusb_init(&g_ctx) != LIBUSB_SUCCESS) {
        logging::write("app", "libusb init failed; hotplug detection unavailable");
        g_ctx = nullptr;
        return false;
    }

    if (libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG) == 0) {
        logging::write("app", "libusb hotplug not supported on this platform");
        libusb_exit(g_ctx);
        g_ctx = nullptr;
        return false;
    }

    g_on_change = std::move(on_change);
    g_stop.store(false);

    const int rc = libusb_hotplug_register_callback(
        g_ctx,
        static_cast<libusb_hotplug_event>(LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
                                          LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT),
        LIBUSB_HOTPLUG_ENUMERATE, // also fire for devices already connected at startup
        kRockchipVid,
        LIBUSB_HOTPLUG_MATCH_ANY, // any product ID (any SoC)
        LIBUSB_HOTPLUG_MATCH_ANY, // any device class
        hotplug_cb, nullptr, &g_cb_handle);

    if (rc != LIBUSB_SUCCESS) {
        logging::write("app", "libusb hotplug registration failed");
        libusb_exit(g_ctx);
        g_ctx = nullptr;
        g_on_change = nullptr;
        return false;
    }

    g_event_thread = std::thread([]() {
        // Servicing events is not device I/O - it just dispatches arrival/
        // departure notifications; the 1s timeout only bounds how quickly the
        // loop notices the stop flag. No rkdeveloptool/USB transfer happens
        // here.
        while (!g_stop.load()) {
            timeval tv{};
            tv.tv_sec = 1;
            libusb_handle_events_timeout_completed(g_ctx, &tv, nullptr);
        }
    });

    logging::write("app", "libusb hotplug monitoring started");
    return true;
}

void stop_usb_monitor() {
    if (g_ctx == nullptr) {
        return;
    }

    g_stop.store(true);
#if defined(LIBUSB_API_VERSION) && (LIBUSB_API_VERSION >= 0x01000105)
    // Wake the event thread immediately instead of waiting out its timeout.
    libusb_interrupt_event_handler(g_ctx);
#endif
    if (g_event_thread.joinable()) {
        g_event_thread.join();
    }

    libusb_hotplug_deregister_callback(g_ctx, g_cb_handle);
    libusb_exit(g_ctx);
    g_ctx = nullptr;
    g_on_change = nullptr;
}

} // namespace hwhelper
