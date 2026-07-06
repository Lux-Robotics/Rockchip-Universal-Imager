#include "core/logging.h"

#include <chrono>
#include <filesystem>
#include <fstream>
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
    out << line << '\n';
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
