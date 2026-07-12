#pragma once

#include <functional>
#include <memory>
#include <string>

namespace saucer {
struct window;
}

namespace rui {

// Installs a native drag-and-drop target covering the window's content view
// that accepts a single .img file dragged from Finder. This has to be native:
// WKWebView surfaces dropped files to JS as File objects with no filesystem
// path, and rkdeveloptool needs a real path. on_drop receives the POSIX path
// and is invoked on the main thread.
void install_file_drop_target(const std::shared_ptr<saucer::window>& window,
                              std::function<void(const std::string& path)> on_drop);

} // namespace rui
