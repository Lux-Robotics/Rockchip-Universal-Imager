#include "core/file_dialog.h"

#include <optional>

#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

#include <windows.h>
#include <cwchar>

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

std::optional<std::string> pick_img_save_file(std::string& error_message) {
    wchar_t file_buffer[MAX_PATH] = {0};
    wcsncpy(file_buffer, L"backup.img", std::size(file_buffer) - 1);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Disk Images (*.img)\0*.img\0";
    ofn.lpstrDefExt = L"img";
    ofn.lpstrFile = file_buffer;
    ofn.nMaxFile = static_cast<DWORD>(std::size(file_buffer));
    ofn.lpstrTitle = L"Save eMMC backup as";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!GetSaveFileNameW(&ofn)) {
        const DWORD err = CommDlgExtendedError();
        if (err != 0) {
            error_message = "file dialog failed";
        }
        return std::nullopt;
    }

    return std::filesystem::path(file_buffer).string();
}
