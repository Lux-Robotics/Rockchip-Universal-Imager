#include "core/quit_guard.h"

// Built for Linux only (macOS has platform/macos/quit_guard.mm).
namespace rui {

void install_quit_guard(std::function<bool()> /*should_block*/,
                        std::function<void()> /*on_blocked*/) {
    // Window-close handling in main is enough on Linux.
}

} // namespace rui
