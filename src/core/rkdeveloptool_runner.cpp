#include "core/rkdeveloptool_runner.h"

#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <reproc++/drain.hpp>
#include <reproc++/reproc.hpp>

#include "core/executable_path.h"
#include "core/logging.h"

namespace rkdev {
namespace {

std::string join_command(const std::vector<std::string>& args) {
    std::string joined = "rkdeveloptool";
    for (const auto& arg : args) {
        joined += " " + arg;
    }
    return joined;
}

std::filesystem::path rkdeveloptool_path() {
    const auto base_dir = hwhelper::executable_dir();
#if defined(_WIN32)
    const auto exe = base_dir / "rkdeveloptool.exe";
    if (std::filesystem::exists(exe)) {
        return exe;
    }
#endif
    const auto bin = base_dir / "rkdeveloptool";
    if (std::filesystem::exists(bin)) {
        return bin;
    }
    throw std::runtime_error("rkdeveloptool not found next to the app");
}

std::vector<std::string> build_arguments(const std::vector<std::string>& args) {
    std::vector<std::string> full;
    full.reserve(args.size() + 1);
    full.push_back(rkdeveloptool_path().string());
    full.insert(full.end(), args.begin(), args.end());
    return full;
}

void emit_lines(std::string& buffer,
                const std::function<void(const std::string&)>& on_line) {
    if (!on_line) {
        buffer.clear();
        return;
    }
    // Progress bars (Read/Write LBA's "(NN%)") are redrawn in place with a
    // bare \r and no trailing \n - splitting on \n alone means every
    // intermediate percentage gets silently concatenated into one buffered
    // string instead of reaching on_line as its own update, so a caller
    // parsing "the first NN% in this line" only ever sees the first one
    // (observed as progress reporting/logging "stuck" at 0%). Split on
    // either.
    size_t pos = 0;
    while ((pos = buffer.find_first_of("\r\n")) != std::string::npos) {
        std::string line = buffer.substr(0, pos);
        on_line(line);
        std::size_t erase_len = pos + 1;
        // Treat a \r\n pair as one separator rather than emitting an extra
        // empty line for the \n half of it.
        if (buffer[pos] == '\r' && pos + 1 < buffer.size() && buffer[pos + 1] == '\n') {
            erase_len = pos + 2;
        }
        buffer.erase(0, erase_len);
    }
}

} // namespace

bool tool_available() {
    try {
        rkdeveloptool_path();
        return true;
    } catch (...) {
        return false;
    }
}

int run_rkdeveloptool(const std::vector<std::string>& args) {
    reproc::process process;
    const std::string command = join_command(args);
    logging::write("rkdeveloptool", "> " + command);

    try {
        const auto full_args = build_arguments(args);
        reproc::options options;
        options.redirect.in.type = reproc::redirect::parent;
        options.redirect.out.type = reproc::redirect::parent;
        options.redirect.err.type = reproc::redirect::parent;

        const auto ec = process.start(full_args, options);
        if (ec) {
            throw std::runtime_error("failed to launch rkdeveloptool: " + ec.message());
        }

        auto [status, wait_ec] = process.wait(reproc::infinite);
        if (wait_ec) {
            throw std::runtime_error("rkdeveloptool wait failed: " + wait_ec.message());
        }
        logging::write("rkdeveloptool", "< " + command + " exit_code=" + std::to_string(status));
        return status;
    } catch (const std::exception& ex) {
        logging::write("rkdeveloptool", "< " + command + " exit_code=-1 error=\"" + ex.what() + "\"");
        throw;
    }
}

RkdevTask::RkdevTask(std::vector<std::string> args,
                     LineCallback on_line,
                     ExitCallback on_exit)
    : args_(std::move(args)),
      on_line_(std::move(on_line)),
      on_exit_(std::move(on_exit))
{
}

RkdevTask::~RkdevTask() {
    cancel();
    if (worker_.joinable()) {
        // The worker thread's lambda captures the owning shared_ptr by value
        // (see start_rkdeveloptool), so that capture - and thus this object -
        // can be destroyed on the worker thread itself, as its very last step,
        // if that capture was the last remaining reference (e.g. g_flash_task
        // was already reassigned to a new task while this one was finishing
        // up). join() on your own thread throws std::system_error, which
        // escapes this (implicitly noexcept) destructor and calls
        // std::terminate - observed crashing exactly this way. Detach instead
        // when that happens; the thread is already returning on its own.
        if (worker_.get_id() == std::this_thread::get_id()) {
            worker_.detach();
        } else {
            worker_.join();
        }
    }
}

void RkdevTask::cancel() {
    cancelled_.store(true);
    // terminate() only confirms the SIGTERM was *sent* - not that the
    // process actually exited. A process blocked inside a blocking libusb
    // transfer can ignore SIGTERM (or not get around to handling it) and
    // keep running, still holding the USB device's interface claimed -
    // which then hangs every subsequent rkdeveloptool invocation that needs
    // the same device (interface claims are exclusive). Always escalate to
    // SIGKILL rather than only when the terminate() signal-send itself
    // failed; there's no cleanup worth waiting for in this tool anyway.
    process_.terminate();
    process_.kill();
}

bool RkdevTask::running() const {
    return running_.load();
}

ProcessResult RkdevTask::result() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return result_;
}

void RkdevTask::set_result(const ProcessResult& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    result_ = result;
    has_result_ = true;
}

void RkdevTask::run() {
    running_.store(true);
    ProcessResult result;

    const std::string command = join_command(args_);
    logging::write("rkdeveloptool", "> " + command);

    auto logging_on_line = [this](const std::string& line) {
        logging::write("rkdeveloptool", line);
        if (on_line_) {
            on_line_(line);
        }
    };

    try {
        const auto full_args = build_arguments(args_);
        reproc::options options;
        options.redirect.in.type = reproc::redirect::parent;
        options.redirect.out.type = reproc::redirect::pipe;
        options.redirect.err.type = reproc::redirect::pipe;

        const auto start_ec = process_.start(full_args, options);
        if (start_ec) {
            throw std::runtime_error("failed to launch rkdeveloptool: " + start_ec.message());
        }

        std::string buffer;
        buffer.reserve(4096);
        const auto sink = [&](reproc::stream, const uint8_t* data, size_t size) -> std::error_code {
            if (size > 0) {
                buffer.append(reinterpret_cast<const char*>(data), size);
                emit_lines(buffer, logging_on_line);
            }
            return {};
        };

        const auto drain_ec = reproc::drain(process_, sink, sink);
        if (drain_ec && drain_ec != std::make_error_code(std::errc::broken_pipe)) {
            throw std::runtime_error("rkdeveloptool output read failed: " + drain_ec.message());
        }

        emit_lines(buffer, logging_on_line);
        if (!buffer.empty()) {
            logging_on_line(buffer);
            buffer.clear();
        }

        auto [status, wait_ec] = process_.wait(reproc::infinite);
        if (wait_ec) {
            throw std::runtime_error("rkdeveloptool wait failed: " + wait_ec.message());
        }

        result.exit_code = status;
        result.was_cancelled = cancelled_.load();
    } catch (const std::exception& ex) {
        result.exit_code = -1;
        result.was_cancelled = cancelled_.load();
        result.error_message = ex.what();
    }

    std::string summary = "< " + command + " exit_code=" + std::to_string(result.exit_code);
    if (result.was_cancelled) {
        summary += " (cancelled)";
    }
    if (!result.error_message.empty()) {
        summary += " error=\"" + result.error_message + "\"";
    }
    logging::write("rkdeveloptool", summary);

    set_result(result);
    running_.store(false);
    if (on_exit_) {
        on_exit_(result);
    }
}

std::shared_ptr<RkdevTask> start_rkdeveloptool(const std::vector<std::string>& args,
                                               RkdevTask::LineCallback on_line,
                                               RkdevTask::ExitCallback on_exit) {
    auto task = std::shared_ptr<RkdevTask>(new RkdevTask(args, std::move(on_line), std::move(on_exit)));
    task->worker_ = std::thread([task]() { task->run(); });
    return task;
}

} // namespace rkdev
