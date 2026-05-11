#pragma once

#include <string>
#include <vector>

#ifdef _WIN32
namespace win_cli {

std::vector<std::wstring> get_command_line_args();
bool has_flag(const std::vector<std::wstring>& args, const std::wstring& flag);
std::wstring get_flag_value(const std::vector<std::wstring>& args, const std::wstring& flag);
std::string wide_to_utf8(const std::wstring& input);

} // namespace win_cli
#endif
