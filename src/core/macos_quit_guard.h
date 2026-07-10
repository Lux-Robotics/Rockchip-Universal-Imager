#pragma once

#include <functional>

namespace hwhelper {

#ifdef __APPLE__
// saucer's window::event::close only fires for a user-initiated window
// close (red button, performClose:). Cmd+Q and the app menu's Quit item go
// through AppKit's default NSApplication terminate: handler instead, which
// saucer never hooks - so neither window::event::close nor
// saucer::application::event::quit fire for that path. This installs our
// own NSApplicationDelegate to catch it directly.
//
// `should_block` is polled synchronously (on the main thread) each time
// termination is attempted; when it returns true, `on_blocked` runs (to
// surface a confirmation prompt) and termination is cancelled for that
// attempt.
void install_quit_guard(std::function<bool()> should_block, std::function<void()> on_blocked);
#endif

} // namespace hwhelper
