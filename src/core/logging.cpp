#include "core/logging.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include "core/executable_path.h"

namespace logging {
namespace {

std::mutex g_mutex;
// Optional mirror for every formatted log line (used to feed the in-app
// live-log panel so it shows exactly what the file records). Guarded by
// g_mutex like everything else here.
std::function<void(const std::string&, bool)> g_sink;
constexpr std::uintmax_t kLogFileCapBytes = 10ULL * 1024ULL * 1024ULL;
// One log file is created per launch; on startup, older launches' files are
// pruned so the directory never accumulates more than this many.
constexpr std::size_t kMaxKeptLogFiles = 20;

bool g_file_logging_enabled = true;
// Kept open for the process lifetime rather than reopened per line - a long
// flash emits a lot of lines, and open/close per line was measurable
// overhead. Flushed per line so the file stays crash-complete and tailable.
std::FILE* g_file = nullptr;
std::uintmax_t g_file_end = 0;
// Byte offset where the last written line starts, valid only while that line
// is a progress line (see write_progress); -1 otherwise.
std::int64_t g_replace_offset = -1;
// Whether the last line delivered anywhere (file and/or sink) was a progress
// line - the sink mirrors the same replace-vs-append decision as the file
// even when file logging is off.
bool g_last_was_progress = false;
// Body (category-prefixed, pre-timestamp) of the last progress line. A
// progress redraw whose text is byte-for-byte identical to this - e.g.
// rkdeveloptool holding at the same "Write LBA from file (NN%)" for many
// milliseconds - is dropped entirely: nothing visible changes, so there's no
// reason to rewrite the file or churn the live-log panel on every redraw.
// Only meaningful while g_last_was_progress is true.
std::string g_last_progress_message;

// A portable build ships an empty marker file named "portable" in its
// companion directory (the folder it was unzipped into). Only the portable
// zips carry it; installers never do.
bool is_portable_build() {
    try {
        std::error_code ec;
        return std::filesystem::exists(rui::companion_dir() / "portable", ec);
    } catch (...) {
        return false;
    }
}

std::filesystem::path log_dir() {
    // Portable builds keep everything self-contained: logs go to a "logs"
    // subfolder of the folder the app was unzipped into (companion_dir, which
    // is beside the .app on macOS), leaving nothing in the user's home. Their
    // directory is user-writable because the user chose where to unzip it.
    if (is_portable_build()) {
        return rui::companion_dir() / "logs";
    }

    // Installed builds go to the per-user, OS-conventional location. Their
    // install directory (/Applications, /usr, Program Files) is not
    // user-writable, so an executable-relative log dir is unusable there.
#if defined(__APPLE__)
    // ~/Library/Logs/RockchipUniversalImager - the standard per-user log location.
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / "Library" / "Logs" / "RockchipUniversalImager";
    }
#elif defined(_WIN32)
    // %LOCALAPPDATA%\RockchipUniversalImager\logs - beside the WebView2 user-data
    // folder set in run_application(); Program Files is not user-writable.
    if (const char* local = std::getenv("LOCALAPPDATA"); local != nullptr && *local != '\0') {
        return std::filesystem::path(local) / "RockchipUniversalImager" / "logs";
    }
#else
    // $XDG_STATE_HOME/rockchip-universal-imager/logs (default ~/.local/state/...): the
    // XDG-designated home for log/state data. The .deb/.rpm install lives in
    // /opt (not user-writable), so its logs belong in this per-user location.
    if (const char* xdg = std::getenv("XDG_STATE_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::filesystem::path(xdg) / "rockchip-universal-imager" / "logs";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".local" / "state" / "rockchip-universal-imager" / "logs";
    }
#endif
    // Last resort only (relevant env var unset): next to the executable.
    return rui::executable_dir() / "log";
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

std::optional<int> log_file_number(const std::filesystem::path& path) {
    if (path.extension() != ".txt") {
        return std::nullopt;
    }
    const auto stem = path.stem().string(); // e.g. "log12"
    if (stem.rfind("log", 0) != 0) {
        return std::nullopt;
    }
    const std::string digits = stem.substr(3);
    if (digits.empty() ||
        !std::all_of(digits.begin(), digits.end(),
                     [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return std::nullopt; // ignore anything that isn't log<number>.txt
    }
    try {
        return std::stoi(digits);
    } catch (...) {
        return std::nullopt;
    }
}

// Next unused logN.txt in the log directory - one sequential file per launch
// (log1.txt, log2.txt, ...). Also prunes the oldest files so the directory
// holds at most kMaxKeptLogFiles including the one about to be created;
// without this the directory (and this startup scan) grew forever.
std::filesystem::path next_sequential_path() {
    const auto dir = log_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    std::vector<std::pair<int, std::filesystem::path>> existing;
    int next = 1;
    if (std::filesystem::exists(dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (const auto n = log_file_number(entry.path())) {
                existing.emplace_back(*n, entry.path());
                if (*n >= next) {
                    next = *n + 1;
                }
            }
        }
    }

    if (existing.size() >= kMaxKeptLogFiles) {
        std::sort(existing.begin(), existing.end());
        const std::size_t to_delete = existing.size() - (kMaxKeptLogFiles - 1);
        for (std::size_t i = 0; i < to_delete; ++i) {
            std::error_code remove_ec;
            std::filesystem::remove(existing[i].second, remove_ec);
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

std::FILE* log_file() {
    if (g_file == nullptr && g_file_logging_enabled) {
        g_file = std::fopen(log_path().string().c_str(), "wb");
        if (g_file == nullptr) {
            g_file_logging_enabled = false;
        }
    }
    return g_file;
}

void truncate_log_file(std::uintmax_t new_size) {
#if defined(_WIN32)
    _chsize_s(_fileno(g_file), static_cast<long long>(new_size));
#else
    ftruncate(fileno(g_file), static_cast<off_t>(new_size));
#endif
}

// `progress` marks the line replaceable: if the previous line was also a
// progress line, this one overwrites it in the file (seek back + truncate)
// and the sink is told to replace rather than append.
void append_line(const std::string& line, bool progress) {
    // Skip an identical consecutive progress redraw - only the percentage (or
    // any other visible text) actually changing is worth an update. This is
    // what keeps a long "Write LBA from file (NN%)" run from rewriting the log
    // line every millisecond while it sits at the same percent.
    if (progress && g_last_was_progress && line == g_last_progress_message) {
        return;
    }

    const std::string stamped = "[" + timestamp() + "] " + line;
    const bool replace_previous = progress && g_last_was_progress;

    if (std::FILE* file = log_file()) {
        std::uintmax_t write_at = g_file_end;
        if (replace_previous && g_replace_offset >= 0) {
            write_at = static_cast<std::uintmax_t>(g_replace_offset);
        }
        if (write_at < kLogFileCapBytes) {
            std::fseek(file, static_cast<long>(write_at), SEEK_SET);
            std::fwrite(stamped.data(), 1, stamped.size(), file);
            std::fputc('\n', file);
            std::fflush(file);
            const std::uintmax_t new_end = write_at + stamped.size() + 1;
            if (new_end < g_file_end) {
                // The replacement line is shorter than the one it overwrote -
                // drop the stale tail rather than leaving garbage after it.
                truncate_log_file(new_end);
            }
            g_replace_offset = progress ? static_cast<std::int64_t>(write_at) : -1;
            g_file_end = new_end;
            if (g_file_end >= kLogFileCapBytes) {
                g_file_logging_enabled = false;
            }
        } else {
            g_file_logging_enabled = false;
        }
    }

    g_last_was_progress = progress;
    g_last_progress_message = progress ? line : std::string();
    if (g_sink) {
        // Same fully-formatted line the file got, so the live-log panel is a
        // faithful mirror. The sink (a webview execute) is fire-and-forget
        // and never re-enters logging, so calling it under g_mutex is safe.
        g_sink(stamped, replace_previous);
    }
}

} // namespace

void write(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    append_line(message, false);
}

void write(const std::string& category, const std::string& message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    append_line("[" + category + "] " + message, false);
}

void write_progress(const std::string& category, const std::string& message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    append_line("[" + category + "] " + message, true);
}

std::string log_directory() {
    const auto dir = log_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

void set_sink(std::function<void(const std::string&, bool)> sink) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sink = std::move(sink);
}

std::string read_all() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file != nullptr) {
        std::fflush(g_file);
    }
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
