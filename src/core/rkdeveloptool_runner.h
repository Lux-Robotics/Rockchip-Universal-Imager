#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <reproc++/reproc.hpp>

namespace rkdev {

struct ProcessResult {
    int exit_code = -1;
    bool was_cancelled = false;
    std::string error_message;
};

int run_rkdeveloptool(const std::vector<std::string>& args);

class RkdevTask : public std::enable_shared_from_this<RkdevTask> {
public:
    using LineCallback = std::function<void(const std::string&)>;
    using ExitCallback = std::function<void(const ProcessResult&)>;

    ~RkdevTask();

    void cancel();
    bool running() const;
    ProcessResult result() const;

private:
    friend std::shared_ptr<RkdevTask> start_rkdeveloptool(const std::vector<std::string>&,
                                                          LineCallback,
                                                          ExitCallback);

    explicit RkdevTask(std::vector<std::string> args,
                       LineCallback on_line,
                       ExitCallback on_exit);

    void run();
    void set_result(const ProcessResult& result);

    std::vector<std::string> args_;
    LineCallback on_line_;
    ExitCallback on_exit_;

    std::atomic<bool> running_{false};
    std::atomic<bool> cancelled_{false};
    std::thread worker_;
    reproc::process process_;

    mutable std::mutex mutex_;
    ProcessResult result_;
    bool has_result_ = false;
};

std::shared_ptr<RkdevTask> start_rkdeveloptool(const std::vector<std::string>& args,
                                               RkdevTask::LineCallback on_line,
                                               RkdevTask::ExitCallback on_exit = {});

} // namespace rkdev
