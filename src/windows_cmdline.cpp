#include "windows_cmdline.h"

#ifdef _WIN32

#include <shellapi.h>
#include <windows.h>

namespace win_cli {

std::vector<std::wstring> get_command_line_args() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    if (!argv) {
        return args;
    }
    args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    LocalFree(argv);
    return args;
}

bool has_flag(const std::vector<std::wstring>& args, const std::wstring& flag) {
    for (const auto& arg : args) {
        if (arg == flag) {
            return true;
        }
    }
    return false;
}

std::wstring get_flag_value(const std::vector<std::wstring>& args, const std::wstring& flag) {
    const std::wstring prefix = flag + L"=";
    for (size_t i = 0; i < args.size(); ++i) {
        const auto& arg = args[i];
        if (arg == flag && i + 1 < args.size()) {
            return args[i + 1];
        }
        if (arg.rfind(prefix, 0) == 0) {
            return arg.substr(prefix.size());
        }
    }
    return L"";
}

std::string wide_to_utf8(const std::wstring& input) {
    if (input.empty()) {
        return std::string();
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, input.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return std::string(input.begin(), input.end());
    }
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, input.c_str(), -1, out.data(), size, nullptr, nullptr);
    out.resize(static_cast<size_t>(size - 1));
    return out;
}

} // namespace win_cli

#endif
