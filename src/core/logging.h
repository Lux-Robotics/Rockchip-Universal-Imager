#pragma once

#include <functional>
#include <string>

namespace logging {

void write(const std::string& message);
void write(const std::string& category, const std::string& message);
std::string read_all();

// Register (or clear, with nullptr) a callback invoked with every
// fully-formatted log line as it's written - used to mirror the persistent
// log into the in-app live-log panel so the two stay identical.
void set_sink(std::function<void(const std::string&)> sink);

} // namespace logging
