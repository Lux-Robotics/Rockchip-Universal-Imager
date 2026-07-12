#pragma once

#include <functional>
#include <string>

namespace logging {

void write(const std::string& message);
void write(const std::string& category, const std::string& message);

// Log a progress-style line (e.g. rkdeveloptool's in-place-redrawn "(NN%)"
// output). Consecutive progress lines REPLACE each other - in the log file
// and in the live-log sink - so a long flash records one continuously
// updated progress line (with a fresh timestamp) instead of tens of
// thousands of appended ones. Any normal write() in between breaks the run
// and the next progress line starts a new one.
void write_progress(const std::string& category, const std::string& message);

std::string read_all();

// Absolute path of the directory the log files live in (see log_dir() for the
// per-platform locations). The directory is created if it doesn't exist yet,
// so the returned path is safe to hand to a "reveal in file manager" action.
std::string log_directory();

// Register (or clear, with nullptr) a callback invoked with every
// fully-formatted log line as it's written - used to mirror the persistent
// log into the in-app live-log panel so the two stay identical.
// `replace_last` is true when this line replaces the previously delivered
// one (consecutive progress updates) rather than appending after it.
void set_sink(std::function<void(const std::string& line, bool replace_last)> sink);

} // namespace logging
