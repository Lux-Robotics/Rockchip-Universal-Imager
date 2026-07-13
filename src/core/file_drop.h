#pragma once

#include <functional>
#include <memory>
#include <string>

namespace saucer {
struct window;
}

namespace rui {

// Installs a native drag-and-drop target that accepts a single .img file and
// delivers a real filesystem path (required by rkdeveloptool). HTML5 File
// drops only expose blob content, not paths, so this must be host-native.
//
// On platforms without a native implementation this is a no-op; the UI still
// supports "Select .img". on_drop runs on the UI/main thread when a drop is
// accepted.
void install_file_drop_target(const std::shared_ptr<saucer::window>& window,
                              std::function<void(const std::string& path)> on_drop);

} // namespace rui
