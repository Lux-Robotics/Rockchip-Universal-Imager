#include "core/udev_rules_helper.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <reproc++/drain.hpp>
#include <reproc++/reproc.hpp>

#include "core/logging.h"

namespace udev_rules {
namespace {

constexpr const char* kRulesPath = "/etc/udev/rules.d/99-rockchip-universal-imager-rockchip.rules";

// MODE 0666 so the device is usable regardless of desktop session/seat;
// TAG+="uaccess" additionally grants the active seat on systemd-logind
// setups. Content must stay single-quote-free - install() embeds it in a
// single-quoted shell string.
constexpr const char* kRulesContent =
    "# Installed by Rockchip Universal Imager - allow non-root access to Rockchip\n"
    "# Maskrom/loader (RockUSB) devices.\n"
    "SUBSYSTEM==\"usb\", ATTR{idVendor}==\"2207\", MODE=\"0666\", TAG+=\"uaccess\"\n";

} // namespace

RulesInfo query() {
    RulesInfo info;
    std::error_code ec;
    info.installed = std::filesystem::exists(kRulesPath, ec);
    if (ec) {
        info.error = "could not check udev rules: " + ec.message();
    }
    return info;
}

InstallResult install() {
    logging::write("app", "Installing udev rules via pkexec");

    const std::string script = std::string("printf '%s' '") + kRulesContent + "' > " + kRulesPath +
                               " && udevadm control --reload-rules && udevadm trigger";

    reproc::process process;
    const std::vector<std::string> args = {"pkexec", "/bin/sh", "-c", script};

    reproc::options options;
    options.redirect.out.type = reproc::redirect::pipe;
    options.redirect.err.type = reproc::redirect::pipe;

    InstallResult result;
    const auto start_ec = process.start(args, options);
    if (start_ec) {
        result.error_message = "failed to start pkexec (is polkit installed?)";
        return result;
    }

    std::string output;
    reproc::drain(
        process,
        [&](reproc::stream, const std::uint8_t* data, std::size_t size) -> std::error_code {
            if (size > 0) {
                output.append(reinterpret_cast<const char*>(data), size);
            }
            return {};
        },
        [&](reproc::stream, const std::uint8_t* data, std::size_t size) -> std::error_code {
            if (size > 0) {
                output.append(reinterpret_cast<const char*>(data), size);
            }
            return {};
        });

    auto [status, wait_ec] = process.wait(reproc::infinite);
    if (wait_ec) {
        result.error_message = "pkexec wait failed: " + wait_ec.message();
        return result;
    }
    // pkexec's own exit codes: 126 = auth dialog dismissed, 127 = not
    // authorized / pkexec failure.
    if (status == 126 || status == 127) {
        result.error_message = "authorization was dismissed";
        return result;
    }
    if (status != 0) {
        while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
            output.pop_back();
        }
        result.error_message = output.empty()
            ? "udev rules install failed (exit " + std::to_string(status) + ")"
            : output;
        return result;
    }

    logging::write("app", "udev rules installed");
    result.success = true;
    return result;
}

} // namespace udev_rules
