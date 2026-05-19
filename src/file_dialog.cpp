#include "file_dialog.h"

#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>

std::optional<std::string> pick_img_file(std::string& error_message) {
    wchar_t file_buffer[MAX_PATH] = {0};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Disk Images (*.img)\0*.img\0";
    ofn.lpstrFile = file_buffer;
    ofn.nMaxFile = static_cast<DWORD>(std::size(file_buffer));
    ofn.lpstrTitle = L"Select .img file";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn)) {
        const DWORD err = CommDlgExtendedError();
        if (err != 0) {
            error_message = "file dialog failed";
        }
        return std::nullopt;
    }

    std::filesystem::path path(file_buffer);
    if (!std::filesystem::exists(path)) {
        error_message = "selected file does not exist";
        return std::nullopt;
    }
    if (path.extension().wstring() != L".img") {
        error_message = "selected file is not a .img";
        return std::nullopt;
    }

    return path.string();
}

#elif defined(__APPLE__)
#include <reproc++/reproc.hpp>
#include <reproc++/drain.hpp>

std::optional<std::string> pick_img_file(std::string& error_message) {
    reproc::process process;
    std::vector<std::string> args = {
        "osascript",
        "-e",
        "POSIX path of (choose file of type {\"img\"} with prompt \"Select .img file\")"
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
    const auto drain_ec = reproc::drain(process, [&](reproc::stream, const uint8_t* data, size_t size) -> std::error_code {
        if (size > 0) {
            output.append(reinterpret_cast<const char*>(data), size);
        }
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

#else
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
    const auto drain_ec = reproc::drain(process, [&](reproc::stream, const uint8_t* data, size_t size) -> std::error_code {
        if (size > 0) {
            output.append(reinterpret_cast<const char*>(data), size);
        }
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
#endif
