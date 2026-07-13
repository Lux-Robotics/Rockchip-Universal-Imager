#include <saucer/embedded/all.hpp>
#include <saucer/smartview.hpp>
#include "core/rkdeveloptool_runner.h"
#include "core/logging.h"
#include "core/device_access.h"
#include "core/file_dialog.h"
#include "core/file_drop.h"
#include "core/loader_map.h"
#include "core/executable_path.h"
#include "core/gpt_probe.h"
#include "core/open_path.h"
#include "core/quit_guard.h"
#include "core/single_instance.h"
#include "core/usb_monitor.h"

#include <atomic>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

struct BackupStartResult {
    bool started = false;
    // Set when no GPT was found and the caller hasn't already confirmed via
    // `force` - `message` holds a human-readable prompt (mentions the size)
    // rather than an error, since nothing has actually failed yet.
    bool needs_confirmation = false;
    std::string message;
};

struct OperationResult {
    bool success = false;
    bool cancelled = false;
    std::string error;
};

struct StorageInfoResult {
    bool success = false;
    std::uint64_t storage_bytes = 0;
    std::string error;
};

struct StorageTargetsResult {
    bool success = false;
    bool emmc_available = false;
    bool sd_available = false;
    bool spinor_available = false;
    unsigned int selected_storage = 0;
    std::string error;
};

struct UsedSpaceResult {
    bool success = false;
    std::uint64_t used_bytes = 0;
    std::string error;
};

struct FilePickResult {
    bool success = false;
    std::string path;
    std::string error;
    std::uint64_t size_bytes = 0;
};

struct DeviceAccessInfoResult {
    // "none" | "windows_driver" | "linux_udev"
    std::string kind;
    bool device_relevant = true;
    bool ready = true;
    std::string detail;
    std::string error;
};

struct DependencyStatusResult {
    bool ok = true;
    std::string warning;
};

struct LogContentsResult {
    bool ok = false;
    std::string text;
};

struct OpenResult {
    bool success = false;
    std::string error;
};

namespace {

std::atomic<bool> g_polling_stop{false};
std::thread g_polling_thread;
std::atomic<bool> g_driver_install_running{false};
std::atomic<bool> g_webview_alive{false};
std::atomic<bool> g_ui_ready{false};
std::atomic<bool> g_flash_running{false};
std::atomic<unsigned int> g_last_detected_vid{0};
std::atomic<unsigned int> g_last_detected_pid{0};

// USB device presence, driven by libusb hotplug (see usb_monitor). The
// hotplug callback (libusb event thread) updates these and signals the CV;
// the device-state worker pushes that presence state to the UI. This replaces
// the old every-2s `rkdeveloptool ld` poll - no periodic USB transfers, only
// work on actual arrival/departure/mode-change events (plus a re-check after
// each flash task completes).
std::atomic<bool> g_device_present{false};
// Set true when the user initiates a Connect (bootloader download). It is
// cleared on departure so each physical connection starts fresh.
std::atomic<bool> g_connect_requested{false};
// True once a loader is confirmed running on the connected device. Set either
// by flashBootloader's `td` pre-check (already-loader case) or by a successful
// `db` command; cleared on departure. The worker reports "connected" purely
// from this flag, so it never needs to re-probe once it's set.
std::atomic<bool> g_loader_ready{false};
constexpr unsigned int kStorageEmmc = 1;
constexpr unsigned int kStorageSd = 2;
constexpr unsigned int kStorageSpiNor = 9;
// Probe order doubles as selection preference; the quick probe stops at the
// first hit, so everything before the selected target is known-absent and
// everything after is probed lazily in the background.
constexpr unsigned int kStorageProbeOrder[] = {kStorageEmmc, kStorageSd, kStorageSpiNor};
std::atomic<unsigned int> g_available_storage_mask{0};
std::atomic<unsigned int> g_selected_storage{0};
// True once every target in kStorageProbeOrder has been probed for the
// current connection (quick probe found nothing, quick probe stopped at the
// last target, or the lazy background probe finished). Cleared alongside the
// mask on departure/disconnect.
std::atomic<bool> g_storage_probe_complete{false};
// True while the background probe holds g_flash_running. The window-close /
// quit guards ignore a "flash" that is really just this probe - it writes
// nothing, so quitting through it is harmless and must not trigger the
// scary mid-operation prompt.
std::atomic<bool> g_lazy_probe_running{false};
std::mutex g_lazy_probe_thread_mutex;
std::thread g_lazy_probe_thread;
std::mutex g_usb_state_mutex;
std::condition_variable g_usb_state_cv;
bool g_usb_state_dirty = true; // start dirty so the worker does an initial pass

void signal_usb_state_changed() {
    {
        std::lock_guard<std::mutex> lock(g_usb_state_mutex);
        g_usb_state_dirty = true;
    }
    g_usb_state_cv.notify_one();
}
std::shared_ptr<rkdev::RkdevTask> g_flash_task;
std::mutex g_flash_mutex;

// Set false while a flash task's worker thread is alive, true again only
// after its on_exit_ callback has fully returned (including whatever it last
// touched on `webview`). Shutdown blocks on this rather than on
// g_flash_running, which flips false as on_exit_'s *first* step - well
// before it's done referencing the webview.
std::atomic<bool> g_flash_task_finished{true};
std::mutex g_flash_done_mutex;
std::condition_variable g_flash_done_cv;
std::atomic<bool> g_force_quit{false};

std::mutex g_driver_install_mutex;
std::thread g_driver_install_thread;

// Set once query_storage_info() successfully reads the selected target's
// size. secureEraseEmmc/backupEmmc each also need the device's total size
// and used to re-probe
// rfi fresh for it independently; a device queried again shortly after an
// Erase can answer differently (controller not fully settled yet), so a
// second independent probe could silently disagree with what the user was
// already shown as "the device's size". Preferring this cached value keeps
// every place that reports the device's size in agreement by construction.
std::atomic<std::uint64_t> g_last_known_storage_sectors{0};

// Synchronous rkdeveloptool probes (td/rci/rfi and storage reads) can be
// reached from more than one thread, and two concurrent rfi + parse runs
// against the same device were observed to
// segfault (a std::regex-based parser at the time; the parser is now plain
// string scanning, but the one-at-a-time invariant is still worth keeping).
// Serialize all rkdeveloptool probing through this.
std::mutex g_rkdev_probe_mutex;

// A cancelled operation (e.g. Backup) can leave a stuck rkdeveloptool
// process holding the USB device's interface claimed for a while even after
// RkdevTask::cancel() escalates to SIGKILL, and the device itself can be
// left momentarily unresponsive after an aborted transfer. Bound every
// synchronous probe below so a wedged device can't hang the app forever -
// worst case it's an error message instead of a frozen window.
constexpr auto kProbeTimeout = std::chrono::seconds(5);

struct RkdevCommandOutput {
    int exit_code = -1;
    bool timed_out = false;
    std::string output;
};

// Run a short rkdeveloptool command synchronously and collect its output.
// RkdevTask owns the command/line/exit logging, so every invocation through
// this helper is recorded in the same persistent log as flash operations.
RkdevCommandOutput run_rkdev_command(const std::vector<std::string>& args) {
    struct WaitState {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        int exit_code = -1;
        std::string output;
    };
    // Heap-allocated and captured by shared_ptr (not by reference into this
    // function's stack frame): if the wait below times out, this function
    // returns before the callbacks necessarily have - a still-running
    // process's eventual on_line_/on_exit_ call then needs somewhere safe to
    // write rather than a destroyed stack frame.
    auto state = std::make_shared<WaitState>();

    auto task = rkdev::start_rkdeveloptool(
        args,
        [state](const std::string& line) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->output += line + "\n";
        },
        [state](const rkdev::ProcessResult& proc_result) {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!proc_result.error_message.empty()) {
                state->output += "Error: " + proc_result.error_message + "\n";
            }
            state->exit_code = proc_result.exit_code;
            state->done = true;
            state->cv.notify_one();
        });

    RkdevCommandOutput result;
    std::unique_lock<std::mutex> lock(state->mutex);
    if (!state->cv.wait_for(lock, kProbeTimeout, [&] { return state->done; })) {
        lock.unlock();
        task->cancel();
        result.timed_out = true;
        return result;
    }
    result.exit_code = state->exit_code;
    result.output = std::move(state->output);
    return result;
}

std::string run_rfi_output() {
    return run_rkdev_command({"rfi"}).output;
}

// Secure Erase's sparse all-zeros source file. Shared between the handler
// that creates it and the startup/post-operation cleanup paths.
std::filesystem::path secure_erase_zero_path() {
    return std::filesystem::temp_directory_path() / "rui_secure_erase_zero.img";
}

constexpr unsigned int storage_bit(unsigned int storage) {
    return 1U << storage;
}

bool is_known_storage(unsigned int storage) {
    return storage == kStorageEmmc || storage == kStorageSd || storage == kStorageSpiNor;
}

const char* storage_name(unsigned int storage) {
    switch (storage) {
    case kStorageEmmc:
        return "eMMC";
    case kStorageSd:
        return "SD card";
    case kStorageSpiNor:
        return "SPI NOR";
    default:
        return "storage";
    }
}

StorageTargetsResult current_storage_targets() {
    const unsigned int mask = g_available_storage_mask.load();
    StorageTargetsResult result;
    result.success = mask != 0;
    result.emmc_available = (mask & storage_bit(kStorageEmmc)) != 0;
    result.sd_available = (mask & storage_bit(kStorageSd)) != 0;
    result.spinor_available = (mask & storage_bit(kStorageSpiNor)) != 0;
    result.selected_storage = g_selected_storage.load();
    if (!result.success) {
        result.error = "no supported storage detected";
    }
    return result;
}

// rkdeveloptool's `cs` exits successfully only after it has read the selected
// target back from the loader. Keep every storage probe behind the existing
// one-at-a-time lock: changing storage affects all subsequent RockUSB I/O.
bool change_storage_locked(unsigned int storage) {
    return run_rkdev_command({"cs", std::to_string(storage)}).exit_code == 0;
}

// Quick probe: walk the preference order (eMMC, SD, SPI NOR) and stop at the
// first target that accepts `cs` - the device is then already switched to
// it, so the common case (an eMMC device) costs exactly one rkdeveloptool
// round-trip instead of one per possible target. Targets later in the order
// stay unknown until start_lazy_storage_probe fills them in off the connect
// path. A user selection later calls select_storage().
StorageTargetsResult probe_storage_targets() {
    std::lock_guard<std::mutex> lock(g_rkdev_probe_mutex);
    unsigned int available_mask = 0;
    unsigned int selected = 0;
    std::size_t probed = 0;
    for (const unsigned int storage : kStorageProbeOrder) {
        ++probed;
        if (change_storage_locked(storage)) {
            available_mask |= storage_bit(storage);
            selected = storage;
            break;
        }
    }

    g_available_storage_mask.store(available_mask);
    g_selected_storage.store(selected);
    g_last_known_storage_sectors.store(0);
    g_storage_probe_complete.store(probed >= std::size(kStorageProbeOrder));

    auto result = current_storage_targets();
    if (selected != 0) {
        logging::write("app", std::string("Storage probe: selected ") + storage_name(selected));
    } else {
        logging::write("app", "no storage devices detected");
    }
    return result;
}

// Probe the targets the quick probe never reached, off the connect path. The
// device is claimed via g_flash_running exactly like a flash task so nothing
// else can touch it mid-probe; if the user got an operation in first, skip -
// start_flash_task's on_exit re-invokes this once the device is free again.
void start_lazy_storage_probe(const std::shared_ptr<saucer::smartview>& view) {
    if (g_storage_probe_complete.load() || !g_loader_ready.load()) {
        return;
    }
    g_lazy_probe_running.store(true);
    bool expected = false;
    if (!g_flash_running.compare_exchange_strong(expected, true)) {
        g_lazy_probe_running.store(false);
        return;
    }

    std::weak_ptr<saucer::smartview> weak_view = view;
    std::thread worker([weak_view]() {
        {
            std::lock_guard<std::mutex> lock(g_rkdev_probe_mutex);
            const unsigned int selected = g_selected_storage.load();
            unsigned int mask = g_available_storage_mask.load();
            bool past_selected = selected == 0;
            for (const unsigned int storage : kStorageProbeOrder) {
                if (!past_selected) {
                    // Everything up to and including the quick probe's stop
                    // point is already known (absent, or the selected hit).
                    past_selected = storage == selected;
                    continue;
                }
                if (!g_device_present.load() || !g_loader_ready.load()) {
                    break;
                }
                if (change_storage_locked(storage)) {
                    mask |= storage_bit(storage);
                }
            }
            // Probing switches the active target; put it back where the quick
            // probe (or the user) left it before releasing the device.
            if (selected != 0 && !change_storage_locked(selected)) {
                mask &= ~storage_bit(selected);
                if (g_selected_storage.load() == selected) {
                    g_selected_storage.store(0);
                }
            }
            g_available_storage_mask.store(mask);
            g_storage_probe_complete.store(true);
        }
        g_flash_running.store(false);
        g_lazy_probe_running.store(false);
        signal_usb_state_changed();
        if (g_webview_alive.load()) {
            if (auto view = weak_view.lock()) {
                view->execute("window.onStorageTargetsUpdated && window.onStorageTargetsUpdated()");
            }
        }
    });

    std::lock_guard<std::mutex> lock(g_lazy_probe_thread_mutex);
    if (g_lazy_probe_thread.joinable()) {
        g_lazy_probe_thread.join();
    }
    g_lazy_probe_thread = std::move(worker);
}

bool select_storage(unsigned int storage) {
    if (!is_known_storage(storage) ||
        (g_available_storage_mask.load() & storage_bit(storage)) == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_rkdev_probe_mutex);
    if (!change_storage_locked(storage)) {
        // Availability is dynamic for removable SD media. A failed reselect
        // means the UI must no longer advertise this target as usable.
        g_available_storage_mask.fetch_and(~storage_bit(storage));
        if (g_selected_storage.load() == storage) {
            g_selected_storage.store(0);
        }
        g_last_known_storage_sectors.store(0);
        return false;
    }

    g_selected_storage.store(storage);
    g_last_known_storage_sectors.store(0);
    logging::write("app", std::string("Storage selected: ") + storage_name(storage));
    return true;
}

// Plain string scanning rather than std::regex: parse_flash_size_sectors is
// reachable from many threads (polling, several exposed endpoints), and despite
// every call site holding g_rkdev_probe_mutex, a std::regex-based version of
// this function has been the reproducible site of two separate SIGSEGVs
// inside libc++'s regex/memmove internals. Avoiding regex here removes that
// risk outright rather than continuing to chase it.
std::optional<std::string> extract_number_before_unit(const std::string& text, const std::string& label, const std::string& unit) {
    std::size_t search_pos = 0;
    while (true) {
        const auto label_pos = text.find(label, search_pos);
        if (label_pos == std::string::npos) {
            return std::nullopt;
        }
        std::size_t pos = label_pos + label.size();
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
        const std::size_t digits_start = pos;
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
        if (pos == digits_start) {
            search_pos = label_pos + label.size();
            continue;
        }
        std::size_t unit_pos = pos;
        while (unit_pos < text.size() && std::isspace(static_cast<unsigned char>(text[unit_pos]))) {
            ++unit_pos;
        }
        if (text.compare(unit_pos, unit.size(), unit) == 0) {
            return text.substr(digits_start, pos - digits_start);
        }
        search_pos = label_pos + label.size();
    }
}

bool parse_flash_size_sectors(const std::string& rfi_output, std::uint64_t& out_sectors) {
    const auto digits = extract_number_before_unit(rfi_output, "Flash Size:", "Sectors");
    if (!digits) {
        return false;
    }
    try {
        out_sectors = std::stoull(*digits);
        return true;
    } catch (...) {
        return false;
    }
}

// Authoritative "is a loader running and servicing RockUSB commands" check.
// `td` is rkdeveloptool's dedicated TestDevice operation: unlike USB
// descriptors (or `ld` text), it checks the protocol endpoint that subsequent
// commands use. It is therefore the sole mode probe; `rfi` remains a storage
// information query rather than being overloaded as a readiness test.
bool probe_loader_ready() {
    std::lock_guard<std::mutex> lock(g_rkdev_probe_mutex);
    return run_rkdev_command({"td"}).exit_code == 0;
}

// Chip Info is diagnostic information requested for each successful Connect.
// Its output is emitted by RkdevTask's normal per-line logger, including the
// `Chip Info: ...` line produced by `rkdeveloptool rci`.
void log_chip_info() {
    std::lock_guard<std::mutex> lock(g_rkdev_probe_mutex);
    (void)run_rkdev_command({"rci"});
}

StorageInfoResult query_storage_info() {
    StorageInfoResult result;

    const unsigned int storage = g_selected_storage.load();
    if (storage == 0) {
        result.error = "no storage target selected";
        return result;
    }

    if (storage != kStorageEmmc) {
        result.success = true;
        result.storage_bytes = 0;
        return result;
    }

    // Prefer a prior successful storage query - size cannot change while the
    // device stays connected, so there is no need for another rfi round-trip.
    if (const auto cached = g_last_known_storage_sectors.load(); cached != 0) {
        result.storage_bytes = cached * 512;
        result.success = true;
        return result;
    }

    std::lock_guard<std::mutex> lock(g_rkdev_probe_mutex);
    std::uint64_t total_sectors = 0;
    if (!parse_flash_size_sectors(run_rfi_output(), total_sectors) || total_sectors == 0) {
        // Most commonly: device is still in Maskrom (no loader downloaded
        // yet), which doesn't implement this query.
        result.error = std::string("could not read ") + storage_name(storage) + " size";
        return result;
    }

    g_last_known_storage_sectors.store(total_sectors);
    result.storage_bytes = total_sectors * 512;
    result.success = true;
    return result;
}

UsedSpaceResult calculate_used_space() {
    UsedSpaceResult result;
    std::lock_guard<std::mutex> lock(g_rkdev_probe_mutex);

    const unsigned int storage = g_selected_storage.load();
    if (storage == 0) {
        result.error = "no storage target selected";
        return result;
    }

    // Reuse a size already established by a storage query instead of
    // re-running rfi here - it cannot change while connected, and this is one
    // fewer rkdeveloptool round-trip before the (many) rl probes.
    std::uint64_t total_sectors = g_last_known_storage_sectors.load();
    if (total_sectors == 0 &&
        (!parse_flash_size_sectors(run_rfi_output(), total_sectors) || total_sectors == 0)) {
        result.error = std::string("could not read ") + storage_name(storage) + " size";
        return result;
    }

    const auto used_sectors = rui::find_used_sector_boundary(total_sectors);
    result.used_bytes = used_sectors * 512;
    result.success = true;
    logging::write("app", "Calculate Used Space: " + std::to_string(result.used_bytes) + " bytes");
    return result;
}

void start_device_polling(const std::shared_ptr<saucer::smartview>& view) {
    if (g_polling_thread.joinable()) {
        return;
    }

    // libusb hotplug drives presence + VID/PID; the callback (on the libusb
    // event thread) records them and wakes the worker below. No rkdeveloptool
    // transfer happens for detection. The `td` readiness probe runs only when
    // the user explicitly connects.
    if (!rui::start_usb_monitor([](bool present, unsigned short vid, unsigned short pid) {
            if (present) {
                g_last_detected_vid.store(vid);
                g_last_detected_pid.store(pid);
            }
            g_device_present.store(present);
            signal_usb_state_changed();
        })) {
        logging::write("app", "hotplug unavailable - device detection is disabled");
    }

    std::weak_ptr<saucer::smartview> weak_view = view;
    g_polling_thread = std::thread([weak_view]() {
        std::string last_status;
        std::string last_soc;
        std::string last_info;
        // Persists across brief departures (a Maskrom device can spontaneously
        // re-enumerate), so we log "detected <SoC>" once per actual device
        // rather than again on every re-arrival of the same one.
        std::string last_logged_soc;

        while (true) {
            {
                std::unique_lock<std::mutex> lock(g_usb_state_mutex);
                g_usb_state_cv.wait(lock, [] { return g_usb_state_dirty || g_polling_stop.load(); });
                g_usb_state_dirty = false;
            }
            if (g_polling_stop.load()) {
                break;
            }

            // A flash/connect/erase task owns the device. Skip - start_flash_task's
            // on_exit re-signals us to re-evaluate once it's done (and the
            // Maskrom->loader re-enumeration fires its own hotplug event too).
            if (g_flash_running.load()) {
                continue;
            }

            const bool present = g_device_present.load();
            const bool tool_missing = !rkdev::tool_available();

            if (!present) {
                g_connect_requested.store(false);
                g_loader_ready.store(false);
                // Drop target state and cached capacity so a fresh connection
                // cannot report a previous device's storage.
                g_available_storage_mask.store(0);
                g_selected_storage.store(0);
                g_last_known_storage_sectors.store(0);
                g_storage_probe_complete.store(false);
            }
            // The worker never invokes rkdeveloptool: loader readiness is
            // established entirely by the connect flow (`td` for an
            // already-running loader, or a successful `db` for Maskrom). This
            // thread only turns those flags into UI status.
            const bool is_ready = g_loader_ready.load();

            std::string status;
            if (present && is_ready) {
                status = "connected";
            } else if (present) {
                status = "detected";
            } else if (tool_missing) {
                status = "tool_missing";
            } else {
                status = "disconnected";
            }

            // Resolve the SoC name from the detected VID/PID. A present device
            // with an unrecognized PID is surfaced as "unknown SoC" rather
            // than showing nothing.
            std::string soc;
            unsigned short vid = 0;
            unsigned short pid = 0;
            if (present) {
                vid = static_cast<unsigned short>(g_last_detected_vid.load());
                pid = static_cast<unsigned short>(g_last_detected_pid.load());
                if (vid != 0 && pid != 0) {
                    if (const char* name = soc_name_for_vid(vid, pid)) {
                        soc = name;
                    } else {
                        soc = "unknown SoC";
                    }
                }
            }

            // Synthesize the info tooltip that used to come from the raw `ld`
            // line, now that we no longer run `ld`.
            std::string info;
            if (present && vid != 0 && pid != 0) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "Vid=0x%04X Pid=0x%04X%s%s%s",
                              vid, pid,
                              soc.empty() ? "" : " (", soc.c_str(), soc.empty() ? "" : ")");
                info = buf;
            }

            if (status != last_status || soc != last_soc || info != last_info) {
                if (!soc.empty() && soc != last_logged_soc) {
                    char idbuf[64];
                    std::snprintf(idbuf, sizeof(idbuf), " (VID 0x%04X, PID 0x%04X)", vid, pid);
                    logging::write("app", "detected " + soc + idbuf);
                    last_logged_soc = soc;
                }
                last_status = status;
                last_soc = soc;
                last_info = info;

                if (auto view = weak_view.lock()) {
                    view->execute("window.updateDeviceStatus && window.updateDeviceStatus({})", status);
                    view->execute("window.updateDeviceInfo && window.updateDeviceInfo({})", info);
                    view->execute("window.updateDeviceSoc && window.updateDeviceSoc({})", soc);
                }
            }
        }
    });
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
            if (kLoaderMap[i].filename == nullptr) {
                // Recognized SoC, but no loader is bundled for it yet.
                error = std::string("no loader bundled for ") + kLoaderMap[i].soc +
                        " - add its SPL loader to loader_binaries/ and map it in loader_map.h";
                return std::nullopt;
            }
            const std::filesystem::path path = rui::resource_dir() / "loader_binaries" / kLoaderMap[i].filename;
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

enum class TaskCompletionAction {
    None,
    Connect,
    Disconnect,
};

bool start_flash_task(const std::shared_ptr<saucer::smartview>& view,
                      const std::vector<std::string>& args,
                      TaskCompletionAction completion_action = TaskCompletionAction::None,
                      std::function<void()> cleanup = {}) {
    bool expected = false;
    if (!g_flash_running.compare_exchange_strong(expected, true)) {
        return false;
    }
    g_flash_task_finished.store(false);

    update_flash_progress(view, 0);

    // rkdeveloptool redraws its progress line many times per second, almost
    // always at the same integer percent. Only forward a change to the UI when
    // the percent actually moves so we don't fire a webview round-trip per
    // redraw. Starts at -1 so the very first parsed percent always lands.
    auto last_percent = std::make_shared<int>(-1);
    auto on_line = [view, last_percent](const std::string& line) {
        // rkdeveloptool output lines reach the live-log panel via the logging
        // sink (RkdevTask logs every line, with consecutive progress redraws
        // replacing each other), so nothing to mirror or re-log here - just
        // drive the progress bar.
        if (const auto percent = parse_progress_percent(line)) {
            if (*percent != *last_percent) {
                *last_percent = *percent;
                update_flash_progress(view, *percent);
            }
        }
    };

    auto on_exit = [view, completion_action, cleanup = std::move(cleanup)](const rkdev::ProcessResult& result) {
        {
            // Don't leave the last-completed task pinned in g_flash_task
            // forever - nothing ever clears it otherwise, so it (and its
            // captured lambdas/process handle) would just sit retained until
            // the next flash operation overwrites it, or the app exits.
            std::lock_guard<std::mutex> lock(g_flash_mutex);
            g_flash_task.reset();
        }

        const bool cancelled = result.was_cancelled;
        const bool success = (result.exit_code == 0 && result.error_message.empty() && !cancelled);

        if (cleanup) {
            // Per-operation resource cleanup (e.g. Secure Erase's temp zero
            // file) - runs on every completion path, success or not, now that
            // the process no longer has the resource open.
            cleanup();
        }

        if (success && completion_action == TaskCompletionAction::Connect) {
            // `db` has made the loader available. Record Chip Info before the
            // UI is released for further operations; this is deliberately
            // best-effort, since a diagnostics failure must not turn a
            // successful loader download into a failed Connect.
            log_chip_info();
            probe_storage_targets();
            g_loader_ready.store(true);
        } else if (success && completion_action == TaskCompletionAction::Disconnect) {
            // rd normally makes the device leave RockUSB immediately. Clear
            // our logical connection even if a platform delivers that USB
            // departure event late (or not at all).
            g_connect_requested.store(false);
            g_loader_ready.store(false);
            g_available_storage_mask.store(0);
            g_selected_storage.store(0);
            g_last_known_storage_sectors.store(0);
            g_storage_probe_complete.store(false);
        }

        g_flash_running.store(false);

        if (g_webview_alive.load()) {
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
        }

        // Signal completion last, after anything above that could still
        // touch `view` - shutdown waits on this before letting the webview
        // be destroyed.
        {
            std::lock_guard<std::mutex> lock(g_flash_done_mutex);
            g_flash_task_finished.store(true);
        }
        g_flash_done_cv.notify_all();

        // Nudge the device-state worker to re-evaluate now that the device is
        // free again - in particular to pick up mode transitions without
        // waiting on a hotplug event.
        signal_usb_state_changed();

        // If the background target probe never got to run (or got skipped
        // because an operation claimed the device first), retry now that the
        // device is free. No-op once complete for this connection.
        if (g_webview_alive.load()) {
            start_lazy_storage_probe(view);
        }
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

// Platform device-access install (Windows libusb-win32 / Linux udev) on a
// worker thread. Same running flag and completion callback on every OS.
bool start_device_access_install(const std::shared_ptr<saucer::smartview>& view,
                                const std::string& device_name) {
    bool expected = false;
    if (!g_driver_install_running.compare_exchange_strong(expected, true)) {
        return false;
    }

    std::weak_ptr<saucer::smartview> weak_view = view;
    std::thread worker([weak_view, device_name]() {
        device_access::InstallOptions options;
        options.device_name = device_name;
        const auto result = device_access::install(options);
        g_driver_install_running.store(false);

        if (!g_webview_alive.load()) {
            return;
        }
        if (auto view = weak_view.lock()) {
            const OperationResult payload{result.success, false, result.error_message};
            view->execute("window.onDriverInstallComplete && window.onDriverInstallComplete({})", payload);
        }
    });

    // Tracked and joined at shutdown instead of detached: a detached
    // install thread could still be mid-callback (touching `view`) after
    // run_app tears the webview down on window close.
    std::lock_guard<std::mutex> lock(g_driver_install_mutex);
    if (g_driver_install_thread.joinable()) {
        g_driver_install_thread.join();
    }
    g_driver_install_thread = std::move(worker);

    return true;
}

std::string device_access_kind_string(device_access::Kind kind) {
    switch (kind) {
    case device_access::Kind::windows_driver:
        return "windows_driver";
    case device_access::Kind::linux_udev:
        return "linux_udev";
    case device_access::Kind::none:
    default:
        return "none";
    }
}

coco::stray run_app(saucer::application* app) {
    auto window = saucer::window::create(app).value();
    auto webview = std::make_shared<saucer::smartview>(saucer::smartview::create({.window = window}).value());

    g_webview_alive.store(true);

    // Mirror every persistent-log line into the in-app live-log panel so the
    // two are identical - the panel shows exactly the app + rkdeveloptool
    // activity the file records, nothing more. Captured weak so a background
    // thread logging mid-shutdown can't resurrect a webview being torn down.
    //
    // Crucially this posts to the UI thread asynchronously rather than
    // calling view->execute() directly: execute() routes through
    // application::invoke, which BLOCKS on future.get() until the UI thread
    // services it when called from another thread. The synchronous probes
    // (query_storage_info etc.) run on the UI thread while holding
    // g_rkdev_probe_mutex and block waiting on an rkdeveloptool worker; if
    // that worker's own log lines then tried to execute() synchronously,
    // they'd deadlock against the very UI thread they're waiting on (observed
    // as a 5s stall then "could not read storage size"). app->post is
    // fire-and-forget, so the worker never blocks.
    {
        std::weak_ptr<saucer::smartview> weak_log_view = webview;
        logging::set_sink([app, weak_log_view](const std::string& line, bool replace_last) {
            if (!g_webview_alive.load()) {
                return;
            }
            app->post([weak_log_view, line, replace_last]() {
                if (!g_webview_alive.load()) {
                    return;
                }
                if (auto view = weak_log_view.lock()) {
                    view->execute("window.appendLiveLog && window.appendLiveLog({}, {})", line, replace_last);
                }
            });
        });
    }

    // Handlers registered on the webview must not capture the webview
    // shared_ptr by value: the lambdas are stored inside the webview itself,
    // so a strong capture is a self-reference cycle that keeps the smartview
    // alive forever. Each handler locks this instead.
    std::weak_ptr<saucer::smartview> weak_webview = webview;

    webview->expose("uiReady", [weak_webview]() {
        bool expected = false;
        if (g_ui_ready.compare_exchange_strong(expected, true)) {
            if (auto view = weak_webview.lock()) {
                start_device_polling(view);
            }
        }
        return true;
    });

    webview->expose("getLogContents", []() {
        return LogContentsResult{true, logging::read_all()};
    });

    webview->expose("openLogDirectory", []() {
        const std::string dir = logging::log_directory();
        if (dir.empty() || !rui::open_path_in_file_manager(dir)) {
            return OpenResult{false, "could not open log folder"};
        }
        return OpenResult{true, ""};
    });

    // Explicit platform name for any UI that still wants OS labels; device
    // access setup uses getDeviceAccessInfo().kind instead of per-OS endpoints.
    webview->expose("getPlatform", []() -> std::string {
#if defined(_WIN32)
        return "windows";
#elif defined(__APPLE__)
        return "macos";
#else
        return "linux";
#endif
    });

    webview->expose("getDependencyStatus", []() {
        DependencyStatusResult result;

#if defined(__APPLE__)
        const auto companion_dir = rui::companion_dir();
        const auto external_rkdeveloptool = companion_dir / "rkdeveloptool";
        const auto external_libusb = companion_dir / "libusb-1.0.0.dylib";

        std::vector<std::string> missing;
        if (!std::filesystem::exists(external_rkdeveloptool)) {
            missing.emplace_back("rkdeveloptool");
        }
        if (!std::filesystem::exists(external_libusb)) {
            missing.emplace_back("libusb-1.0.0.dylib");
        }
        if (!missing.empty()) {
            result.ok = false;
            result.warning = "Required external dependency missing beside rockchip-universal-imager.app: ";
            for (std::size_t i = 0; i < missing.size(); ++i) {
                if (i != 0) {
                    result.warning += ", ";
                }
                result.warning += missing[i];
            }
            result.warning += " - keep it in the same folder as rockchip-universal-imager.app.";
        }
#else
        if (!rkdev::tool_available()) {
            result.ok = false;
            result.warning = "rkdeveloptool is missing - reinstall or repair the app.";
        }
#endif

        return result;
    });

    webview->expose("getDeviceAccessInfo", []() {
        const auto info = device_access::query();
        return DeviceAccessInfoResult{
            device_access_kind_string(info.kind),
            info.device_relevant,
            info.ready,
            info.detail,
            info.error,
        };
    });

    webview->expose("installDeviceAccess", [weak_webview](const std::string& device_name) {
        auto view = weak_webview.lock();
        if (!view || !start_device_access_install(view, device_name)) {
            return StartResult{false, "install already in progress"};
        }
        return StartResult{true, ""};
    });

    webview->expose("selectImageFile", []() {
        std::string error;
        auto path = pick_img_file(error);
        if (!path) {
            return FilePickResult{false, std::string(), error.empty() ? "file picker canceled" : error};
        }
        std::error_code size_ec;
        const auto size = std::filesystem::file_size(*path, size_ec);
        return FilePickResult{true, *path, "", size_ec ? 0 : static_cast<std::uint64_t>(size)};
    });

    webview->expose("selectBackupDestination", []() {
        std::string error;
        auto path = pick_img_save_file(error);
        if (!path) {
            return FilePickResult{false, std::string(), error.empty() ? "file picker canceled" : error};
        }
        return FilePickResult{true, *path, ""};
    });

    webview->expose("flashBootloader", [weak_webview]() {
        auto webview = weak_webview.lock();
        if (!webview) {
            return StartResult{false, "shutting down"};
        }
        const unsigned int vid = g_last_detected_vid.load();
        const unsigned int pid = g_last_detected_pid.load();
        if (vid == 0 || pid == 0) {
            return StartResult{false, "device VID/PID not detected"};
        }

        g_connect_requested.store(true);

        // The USB descriptor can't tell an already-running loader from Maskrom
        // (an RK3588 with its loader up still reports as Maskrom in bcdUSB and
        // `ld`), so probe with rkdeveloptool's dedicated `td` command.
        // Re-running `db` on a device whose loader is already running can
        // hang, which is why we must know before deciding. Briefly claim
        // g_flash_running so the
        // device worker doesn't touch the device at the same time.
        bool expected = false;
        if (!g_flash_running.compare_exchange_strong(expected, true)) {
            return StartResult{false, "flash already in progress"};
        }
        const bool already_ready = probe_loader_ready();

        if (already_ready) {
            // A loader is already running - no bootloader download needed.
            // Capture its Chip Info as part of every Connect before exposing
            // the connected state to the UI.
            log_chip_info();
            probe_storage_targets();
            g_flash_running.store(false);
            logging::write("app", "Connect: loader already running");
            g_loader_ready.store(true);
            signal_usb_state_changed(); // worker -> "connected"
            update_flash_progress(webview, 100);
            const OperationResult payload{true, false, ""};
            webview->execute("window.onFlashComplete && window.onFlashComplete({})", payload);
            start_lazy_storage_probe(webview);
            return StartResult{true, ""};
        }

        g_flash_running.store(false);

        std::string error;
        auto loader = loader_path_for_vid(static_cast<unsigned short>(vid), static_cast<unsigned short>(pid), error);
        if (!loader) {
            g_connect_requested.store(false);
            return StartResult{false, error};
        }

        logging::write("app", "Connect: downloading bootloader " + *loader);
        if (!start_flash_task(webview, {"db", *loader}, TaskCompletionAction::Connect)) {
            return StartResult{false, "flash already in progress"};
        }

        return StartResult{true, ""};
    });

    webview->expose("disconnectDevice", [weak_webview]() {
        auto webview = weak_webview.lock();
        if (!webview) {
            return StartResult{false, "shutting down"};
        }
        if (!g_loader_ready.load()) {
            return StartResult{false, "device is not connected"};
        }

        logging::write("app", "Disconnect: resetting device");
        if (!start_flash_task(webview, {"rd"}, TaskCompletionAction::Disconnect)) {
            return StartResult{false, "flash already in progress"};
        }
        return StartResult{true, ""};
    });

    webview->expose("flashImage", [weak_webview](const std::string& image_path) {
        auto webview = weak_webview.lock();
        if (!webview) {
            return StartResult{false, "shutting down"};
        }
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

        logging::write("app", "Flash Image: " + image_path);
        if (!start_flash_task(webview, {"wl", "0", image_path})) {
            return StartResult{false, "flash already in progress"};
        }

        return StartResult{true, ""};
    });

    webview->expose("eraseEmmc", [weak_webview]() {
        auto webview = weak_webview.lock();
        if (!webview) {
            return StartResult{false, "shutting down"};
        }
        logging::write("app", "Quick Erase");
        if (!start_flash_task(webview, {"ef"})) {
            return StartResult{false, "flash already in progress"};
        }
        return StartResult{true, ""};
    });

    webview->expose("secureEraseEmmc", [weak_webview]() {
        auto webview = weak_webview.lock();
        if (!webview) {
            return StartResult{false, "shutting down"};
        }
        const unsigned int storage = g_selected_storage.load();
        if (storage == 0) {
            return StartResult{false, "no storage target selected"};
        }
        bool expected = false;
        if (!g_flash_running.compare_exchange_strong(expected, true)) {
            return StartResult{false, "flash already in progress"};
        }
        std::uint64_t total_sectors = 0;
        {
            std::lock_guard<std::mutex> lock(g_rkdev_probe_mutex);
            parse_flash_size_sectors(run_rfi_output(), total_sectors);
        }
        g_flash_running.store(false);

        // Prefer the size already established for the selected target over
        // this fresh probe - see g_last_known_storage_sectors.
        if (const auto cached = g_last_known_storage_sectors.load(); cached != 0) {
            total_sectors = cached;
        }

        if (total_sectors == 0) {
            return StartResult{false, std::string("could not determine ") + storage_name(storage) + " size"};
        }

        // Erase (ef) only issues the device's native erase command, which
        // we've confirmed doesn't guarantee physically overwritten data on
        // this hardware. The only reliable option is an actual overwrite:
        // write zeros across the entire reported capacity via the same path
        // as Flash Image. The source doesn't need to be a real ~30GB file on
        // disk - a sparse file of that length reads back as zeros for any
        // region never actually written, which is exactly what we want, at
        // negligible real disk cost.
        const auto zero_path = secure_erase_zero_path();
        std::error_code remove_ec;
        std::filesystem::remove(zero_path, remove_ec);
        {
            std::ofstream create(zero_path, std::ios::binary | std::ios::trunc);
            if (!create) {
                return StartResult{false, "failed to prepare erase source file"};
            }
        }
        std::error_code resize_ec;
        std::filesystem::resize_file(zero_path, total_sectors * 512, resize_ec);
        if (resize_ec) {
            return StartResult{false, "failed to prepare erase source file: " + resize_ec.message()};
        }

        logging::write("app", "Secure Erase: overwriting " + std::to_string(total_sectors * 512) + " bytes with zeros");
        // Remove the (sparse, but alarmingly large-looking) source file once
        // the write finishes - it previously sat in temp forever.
        auto remove_zero_file = [zero_path]() {
            std::error_code cleanup_ec;
            std::filesystem::remove(zero_path, cleanup_ec);
        };
        if (!start_flash_task(webview, {"wl", "0", zero_path.string()},
                              TaskCompletionAction::None, std::move(remove_zero_file))) {
            return StartResult{false, "flash already in progress"};
        }
        return StartResult{true, ""};
    });

    webview->expose("backupEmmc", [weak_webview](const std::string& dest_path, bool force) {
        auto webview = weak_webview.lock();
        if (!webview) {
            return BackupStartResult{false, false, "shutting down"};
        }
        if (dest_path.empty()) {
            return BackupStartResult{false, false, "no destination selected"};
        }
        const unsigned int storage = g_selected_storage.load();
        if (storage == 0) {
            return BackupStartResult{false, false, "no storage target selected"};
        }

        bool expected = false;
        if (!g_flash_running.compare_exchange_strong(expected, true)) {
            return BackupStartResult{false, false, "flash already in progress"};
        }

        std::uint64_t main_sectors = 0;
        std::uint64_t total_sectors = 0;
        {
            std::lock_guard<std::mutex> lock(g_rkdev_probe_mutex);
            if (const auto gpt = rui::read_gpt_info()) {
                main_sectors = gpt->last_used_lba + 1;
            }
            parse_flash_size_sectors(run_rfi_output(), total_sectors);
        }
        g_flash_running.store(false);

        // Prefer the size already established for the selected target over
        // this fresh probe - see g_last_known_storage_sectors.
        if (const auto cached = g_last_known_storage_sectors.load(); cached != 0) {
            total_sectors = cached;
        }

        if (main_sectors == 0) {
            if (total_sectors == 0) {
                return BackupStartResult{false, false,
                    std::string("could not determine ") + storage_name(storage) + " size"};
            }
            // No valid GPT found, so there's no reliable boundary to trim
            // against (content-scanning turned out not to read back
            // uniformly-blank on unused/erased regions of this hardware,
            // making it unreliable for actually deciding backup size) - most
            // often because Erase eMMC issues a native eMMC erase command
            // rather than actually zeroing sectors, and per the eMMC spec
            // what a controller does with "erased" blocks afterward is
            // implementation-defined; on this hardware they still read back
            // as the previous OS's real bytes rather than a blank pattern.
            // Rather than guess, ask before committing to a full-disk dump -
            // and be explicit that a previous OS's data may still be in there.
            if (!force) {
                const double total_gb = static_cast<double>(total_sectors) * 512.0 / (1024.0 * 1024.0 * 1024.0);
                char buf[320];
                std::snprintf(buf, sizeof(buf),
                    "No partition table was found on this storage target, so it can't be trimmed precisely. "
                    "If this device was previously flashed and erased, its old data may still be physically "
                    "present and could be captured in this backup (erase does not guarantee a secure wipe). "
                    "This will back up the entire %.1f GiB device. Continue?",
                    total_gb);
                return BackupStartResult{false, true, std::string(buf)};
            }
            main_sectors = total_sectors;
        }

        logging::write("app", std::string("Backup ") + storage_name(storage) + ": " +
            std::to_string(main_sectors) + " sectors -> " + dest_path);
        if (!start_flash_task(webview, {"rl", "0", std::to_string(main_sectors), dest_path})) {
            return BackupStartResult{false, false, "flash already in progress"};
        }

        return BackupStartResult{true, false, ""};
    });

    webview->expose("cancelFlash", []() {
        logging::write("app", "Cancel requested");
        if (!cancel_flash_task()) {
            return StartResult{false, "no flash in progress"};
        }
        return StartResult{true, ""};
    });

    webview->expose("forceCloseWindow", [window]() {
        g_force_quit.store(true);
        cancel_flash_task();
        window->close();
        return true;
    });

    webview->expose("getStorageInfo", []() {
        return query_storage_info();
    });

    webview->expose("getStorageTargets", []() {
        return current_storage_targets();
    });

    webview->expose("selectStorage", [](unsigned int storage) {
        if (!g_loader_ready.load()) {
            return StartResult{false, "device is not connected"};
        }
        if (!is_known_storage(storage)) {
            return StartResult{false, "unknown storage target"};
        }
        if ((g_available_storage_mask.load() & storage_bit(storage)) == 0) {
            return StartResult{false, std::string(storage_name(storage)) + " not detected"};
        }

        bool expected = false;
        if (!g_flash_running.compare_exchange_strong(expected, true)) {
            return StartResult{false, "flash already in progress"};
        }
        const bool selected = select_storage(storage);
        g_flash_running.store(false);
        if (!selected) {
            return StartResult{false, std::string(storage_name(storage)) + " not detected"};
        }
        return StartResult{true, ""};
    });

    webview->expose("calculateUsedSpace", []() {
        return calculate_used_space();
    });

    // Veto the native window close while a flash/erase/backup is running and
    // ask the user to confirm in-app instead of letting the OS just tear the
    // window (and the in-flight rkdeveloptool subprocess) down. Captures
    // webview only as a weak_ptr: webview already holds a shared_ptr back to
    // this window, so a strong capture here would form window -> listener ->
    // webview -> window reference cycle that never gets collected.
    std::weak_ptr<saucer::smartview> weak_webview_for_close = webview;
    window->on<saucer::window::event::close>([weak_webview_for_close]() -> saucer::policy {
        // The lazy storage probe claims g_flash_running to keep the device
        // exclusive, but it writes nothing - quitting through it is harmless
        // and must not raise the mid-operation prompt.
        if (!g_flash_running.load() || g_lazy_probe_running.load() || g_force_quit.load()) {
            return saucer::policy::allow;
        }
        if (g_webview_alive.load()) {
            if (auto view = weak_webview_for_close.lock()) {
                view->execute("window.onQuitDuringOperation && window.onQuitDuringOperation()");
            }
        }
        return saucer::policy::block;
    });

    // Fires once the native window has actually closed, on every path
    // (force-quit, a plain close with nothing running, etc.) - flip this
    // here rather than only after run_app's post-finish shutdown wait below.
    // A cancelled task's on_exit_ callback can still fire mid-shutdown (see
    // that wait) and checks g_webview_alive before touching `webview`; if
    // the native window already closed out from under it first (which
    // forceCloseWindow's window->close() can do before app->finish() even
    // resolves) but this flag hadn't caught up yet, that touch could hang
    // indefinitely, which in turn means on_exit_ never reaches the "mark
    // finished" step the shutdown wait blocks on - an unrecoverable freeze.
    window->on<saucer::window::event::closed>([] {
        g_webview_alive.store(false);
    });

    // On macOS, Cmd+Q / the app menu Quit item never reach window::event::close
    // (see core/quit_guard.h). No-op on other platforms.
    rui::install_quit_guard(
        [] { return g_flash_running.load() && !g_lazy_probe_running.load() && !g_force_quit.load(); },
        [weak_webview_for_close]() {
            if (g_webview_alive.load()) {
                if (auto view = weak_webview_for_close.lock()) {
                    view->execute("window.onQuitDuringOperation && window.onQuitDuringOperation()");
                }
            }
        });

    // Native .img path drop where supported (macOS); no-op elsewhere.
    // Weak capture for the same self-cycle reason as the expose handlers above.
    rui::install_file_drop_target(window, [weak_webview](const std::string& path) {
        if (!g_webview_alive.load()) {
            return;
        }
        auto view = weak_webview.lock();
        if (!view) {
            return;
        }
        std::error_code size_ec;
        const auto size = std::filesystem::file_size(path, size_ec);
        const FilePickResult payload{true, path, "", size_ec ? 0 : static_cast<std::uint64_t>(size)};
        view->execute("window.onImageFileDropped && window.onImageFileDropped({})", payload);
    });

    window->set_title("Rockchip Universal Imager");
    window->set_size({.w = 800, .h = 600});

    webview->embed(saucer::embedded::all());
    webview->serve("/index.html");
    window->show();

    co_await app->finish();

    // Stop mirroring log lines into the webview before it's torn down: a
    // background thread (e.g. polling) can still log during the shutdown
    // sequence below, and the sink holds a weak_ptr to this webview.
    logging::set_sink(nullptr);

    // window::event::close already blocks a plain user-initiated close while
    // a flash/erase/backup is running (see above), but this is a safety net
    // for any other path that can end the app mid-operation. Cancel it and
    // wait for its worker thread to fully finish - including on_exit_'s own
    // use of `webview` - before webview/window are destroyed below.
    std::shared_ptr<rkdev::RkdevTask> task_to_cancel;
    {
        std::lock_guard<std::mutex> lock(g_flash_mutex);
        task_to_cancel = g_flash_task;
    }
    if (task_to_cancel && g_flash_running.load()) {
        task_to_cancel->cancel();
    }
    {
        std::unique_lock<std::mutex> lock(g_flash_done_mutex);
        g_flash_done_cv.wait(lock, [] { return g_flash_task_finished.load(); });
    }

    {
        std::lock_guard<std::mutex> lock(g_driver_install_mutex);
        if (g_driver_install_thread.joinable()) {
            g_driver_install_thread.join();
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_lazy_probe_thread_mutex);
        if (g_lazy_probe_thread.joinable()) {
            g_lazy_probe_thread.join();
        }
    }

    g_webview_alive.store(false);

    // Stop the hotplug monitor first so no further arrival/departure events
    // wake the worker after we've asked it to exit.
    rui::stop_usb_monitor();

    g_polling_stop.store(true);
    signal_usb_state_changed(); // wake the worker out of its CV wait so it sees the stop
    if (g_polling_thread.joinable()) {
        g_polling_thread.join();
    }
}

} // namespace

int run_application() {
    int exit_code = 0;
    if (device_access::try_handle_elevated_cli(exit_code)) {
        return exit_code;
    }

    // Refuse to start a second instance: two copies would each drive
    // rkdeveloptool against the same device, and libusb interface claims are
    // exclusive - the two processes race for the handle and wedge each other.
    // Checked before any logging so a blocked second instance doesn't spawn a
    // stray per-launch log file just to exit.
    if (!rui::try_acquire_single_instance()) {
        rui::notify_already_running();
        return 0;
    }

    // Forces the per-launch log file (see logging.cpp) into existence right
    // away rather than lazily on the first rkdeveloptool/UI log line, and
    // gives every log a clear marker of where a session started.
    logging::write("app", "launched");

    // A Secure Erase interrupted by a crash/force-quit can leave its sparse
    // source file behind; the normal cleanup runs when the operation ends,
    // so anything still here is stale.
    {
        std::error_code stale_ec;
        std::filesystem::remove(secure_erase_zero_path(), stale_ec);
    }
#ifdef _WIN32
    wchar_t appdata_path[MAX_PATH];
    const DWORD appdata_len = GetEnvironmentVariableW(
        L"LOCALAPPDATA",
        appdata_path,
        static_cast<DWORD>(std::size(appdata_path)));
    if (appdata_len > 0 && appdata_len < std::size(appdata_path)) {
        std::wstring user_data_dir = appdata_path;
        user_data_dir += L"\\RockchipUniversalImager\\WebView2";
        SetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", user_data_dir.c_str());
    }

#if defined(RUI_DISABLE_GPU)
    SetEnvironmentVariableW(
        L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
        L"--disable-gpu --disable-gpu-compositing --disable-extensions --disable-features=BackForwardCache --no-first-run --disable-background-networking --disable-component-update");
#endif
#elif defined(__APPLE__)
#if defined(RUI_DISABLE_GPU)
    setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 1);
#endif
#elif defined(__linux__)
#if defined(RUI_DISABLE_GPU)
    setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 1);
    setenv("WEBKIT_DISABLE_DMABUF_RENDERER", "1", 1);
#endif
#endif

    auto app = saucer::application::create({.id = "rockchip-universal-imager"});
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
