#pragma once

#include <functional>

namespace rui {

// On macOS, Cmd+Q and the app menu Quit item go through NSApplication
// terminate: and never hit saucer's window::event::close. This installs an
// NSApplicationDelegate to intercept that path. On other platforms this is a
// no-op (window close is sufficient).
//
// `should_block` is polled on the main thread when termination is attempted;
// when true, `on_blocked` runs (e.g. show a JS confirm) and termination is
// cancelled for that attempt.
void install_quit_guard(std::function<bool()> should_block, std::function<void()> on_blocked);

} // namespace rui
