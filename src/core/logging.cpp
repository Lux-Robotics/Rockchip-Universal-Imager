#include "core/logging.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

#include "core/executable_path.h"

namespace logging {
namespace {

std::mutex g_mutex;
// Optional mirror for every formatted log line (used to feed the in-app
// live-log panel so it shows exactly what the file records). Guarded by
// g_mutex like everything else here.
std::function<void(const std::string&)> g_sink;
constexpr std::uintmax_t kLogFileCapBytes = 10ULL * 1024ULL * 1024ULL;

bool g_file_logging_enabled = true;

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

// Next unused logN.txt in the log directory - one sequential file per
// launch (log1.txt, log2.txt, ...), no date in the name.
std::filesystem::path next_sequential_path() {
    const auto dir = log_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    int next = 1;
    if (std::filesystem::exists(dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.path().extension() != ".txt") {
                continue;
            }
            const auto stem = entry.path().stem().string(); // e.g. "log12"
            if (stem.rfind("log", 0) != 0) {
                continue;
            }
            const std::string digits = stem.substr(3);
            if (digits.empty() ||
                !std::all_of(digits.begin(), digits.end(),
                             [](unsigned char c) { return std::isdigit(c) != 0; })) {
                continue; // ignore anything that isn't log<number>.txt
            }
            try {
                const int n = std::stoi(digits);
                if (n >= next) {
                    next = n + 1;
                }
            } catch (...) {
            }
        }
    }
    return dir / ("log" + std::to_string(next) + ".txt");
}

const std::filesystem::path& log_path() {
    // Computed once and cached for the life of the process (function-local
    // static): one sequential file per launch, chosen at first use.
    static const std::filesystem::path path = next_sequential_path();
    return path;
}

void append_line(const std::string& line) {
    const std::string stamped = "[" + timestamp() + "] " + line;
    std::filesystem::create_directories(log_dir());
    if (g_file_logging_enabled) {
        std::error_code size_ec;
        const auto path = log_path();
        const auto current_size = std::filesystem::exists(path, size_ec)
            ? std::filesystem::file_size(path, size_ec)
            : 0;
        if (!size_ec && current_size < kLogFileCapBytes) {
            std::ofstream out(path, std::ios::app);
            if (out) {
                out << stamped << '\n';
                if (static_cast<std::uintmax_t>(current_size + stamped.size() + 1) >= kLogFileCapBytes) {
                    g_file_logging_enabled = false;
                }
            }
        } else {
            g_file_logging_enabled = false;
        }
    }
    if (g_sink) {
        // Same fully-formatted line the file got, so the live-log panel is a
        // faithful mirror. The sink (a webview execute) is fire-and-forget
        // and never re-enters logging, so calling it under g_mutex is safe.
        g_sink(stamped);
    }
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

void set_sink(std::function<void(const std::string&)> sink) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sink = std::move(sink);
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
