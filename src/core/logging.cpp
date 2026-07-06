#include "core/logging.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

#include "core/executable_path.h"

namespace logging {
namespace {

std::mutex g_mutex;

std::filesystem::path log_dir() {
    return hwhelper::executable_dir() / "log";
}

// Millisecond-precision so interleaved log lines from the polling thread,
// flash-task worker thread, and UI-driven calls can actually be ordered
// when debugging races between them.
std::string timestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t t = system_clock::to_time_t(now);
    std::tm tm_snapshot{};
#if defined(_WIN32)
    localtime_s(&tm_snapshot, &t);
#else
    localtime_r(&t, &tm_snapshot);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm_snapshot);
    std::ostringstream oss;
    oss << buf << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string date_stamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(now);
    std::tm tm_snapshot{};
#if defined(_WIN32)
    localtime_s(&tm_snapshot, &t);
#else
    localtime_r(&t, &tm_snapshot);
#endif
    char buf[11];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_snapshot);
    return std::string(buf);
}

std::filesystem::path log_path() {
    return log_dir() / ("log" + date_stamp() + ".txt");
}

void append_line(const std::string& line) {
    std::filesystem::create_directories(log_dir());
    std::ofstream out(log_path(), std::ios::app);
    out << "[" << timestamp() << "] " << line << '\n';
}

} // namespace

void write(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    append_line(message);
}

void write(const std::string& category, const std::string& message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    append_line("[" + category + "] " + message);
}

std::string read_all() {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto path = log_path();
    if (!std::filesystem::exists(path)) {
        return std::string();
    }
    std::ifstream in(path, std::ios::in);
    if (!in) {
        return std::string();
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace logging
