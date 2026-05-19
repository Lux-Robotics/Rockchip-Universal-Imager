#pragma once

#include <string>

namespace logging {

void write(const std::string& message);
void write(const std::string& category, const std::string& message);
std::string read_all();

} // namespace logging
