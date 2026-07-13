#include "core/file_dialog.h"

#include <optional>

#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

#include <reproc++/reproc.hpp>
#include <reproc++/drain.hpp>

std::optional<std::string> pick_img_file(std::string& error_message) {
    reproc::process process;
    std::vector<std::string> args = {
        "zenity",
        "--file-selection",
        "--file-filter=*.img",
        "--title=Select .img file"
    };

    reproc::options options;
    options.redirect.out.type = reproc::redirect::pipe;
    options.redirect.err.type = reproc::redirect::pipe;

    const auto start_ec = process.start(args, options);
    if (start_ec) {
        error_message = "failed to start file picker";
        return std::nullopt;
    }

    std::string output;
    const auto drain_ec = reproc::drain(
        process,
        [&](reproc::stream, const uint8_t* data, size_t size) -> std::error_code {
            if (size > 0) {
                output.append(reinterpret_cast<const char*>(data), size);
            }
            return {};
        },
        [&](reproc::stream, const uint8_t*, size_t) -> std::error_code {
            return {};
        });

    auto [status, wait_ec] = process.wait(reproc::infinite);
    if (drain_ec || wait_ec || status != 0) {
        error_message = "file picker canceled";
        return std::nullopt;
    }

    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }

    if (output.empty()) {
        error_message = "file picker canceled";
        return std::nullopt;
    }
    if (std::filesystem::path(output).extension() != ".img") {
        error_message = "selected file is not a .img";
        return std::nullopt;
    }

    return output;
}

std::optional<std::string> pick_img_save_file(std::string& error_message) {
    reproc::process process;
    std::vector<std::string> args = {
        "zenity",
        "--file-selection",
        "--save",
        "--confirm-overwrite",
        "--file-filter=*.img",
        "--title=Save eMMC backup as",
        "--filename=backup.img"
    };

    reproc::options options;
    options.redirect.out.type = reproc::redirect::pipe;
    options.redirect.err.type = reproc::redirect::pipe;

    const auto start_ec = process.start(args, options);
    if (start_ec) {
        error_message = "failed to start file picker";
        return std::nullopt;
    }

    std::string output;
    const auto drain_ec = reproc::drain(
        process,
        [&](reproc::stream, const uint8_t* data, size_t size) -> std::error_code {
            if (size > 0) {
                output.append(reinterpret_cast<const char*>(data), size);
            }
            return {};
        },
        [&](reproc::stream, const uint8_t*, size_t) -> std::error_code {
            return {};
        });

    auto [status, wait_ec] = process.wait(reproc::infinite);
    if (drain_ec || wait_ec || status != 0) {
        error_message = "file picker canceled";
        return std::nullopt;
    }

    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }

    if (output.empty()) {
        error_message = "file picker canceled";
        return std::nullopt;
    }
    if (std::filesystem::path(output).extension() != ".img") {
        output += ".img";
    }

    return output;
}
