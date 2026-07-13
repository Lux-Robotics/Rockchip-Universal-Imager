#include "core/quit_guard.h"

namespace rui {

void install_quit_guard(std::function<bool()> /*should_block*/,
                        std::function<void()> /*on_blocked*/) {
    // Window-close handling in main is enough on Windows.
}

} // namespace rui
