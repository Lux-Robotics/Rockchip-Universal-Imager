#include "rkdeveloptool_runner.h"

#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <reproc++/drain.hpp>
#include <reproc++/reproc.hpp>

namespace rkdev {
namespace {

std::filesystem::path rkdeveloptool_path() {
    const auto base_dir = std::filesystem::current_path();
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
    size_t pos = 0;
    while ((pos = buffer.find('\n')) != std::string::npos) {
        std::string line = buffer.substr(0, pos);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        on_line(line);
        buffer.erase(0, pos + 1);
    }
}

} // namespace

int run_rkdeveloptool(const std::vector<std::string>& args) {
    reproc::process process;
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
    return status;
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
        worker_.join();
    }
}

void RkdevTask::cancel() {
    cancelled_.store(true);
    auto ec = process_.terminate();
    if (ec) {
        process_.kill();
    }
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
                emit_lines(buffer, on_line_);
            }
            return {};
        };

        const auto drain_ec = reproc::drain(process_, sink, sink);
        if (drain_ec && drain_ec != std::make_error_code(std::errc::broken_pipe)) {
            throw std::runtime_error("rkdeveloptool output read failed: " + drain_ec.message());
        }

        emit_lines(buffer, on_line_);
        if (!buffer.empty() && on_line_) {
            on_line_(buffer);
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
