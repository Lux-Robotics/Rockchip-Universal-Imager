#pragma once

namespace rui {

// Attempts to become the single running instance of the app. Returns true if
// this process acquired the lock (i.e. it's the only/first instance), false
// if another instance already holds it.
//
// The lock is held for the remainder of the process lifetime and released
// automatically on exit - including a crash - so there's no stale-lock
// recovery to worry about. Fails open (returns true) if the lock mechanism
// itself can't be set up, so a filesystem/OS quirk never blocks launching.
bool try_acquire_single_instance();

// Shows a minimal native "already running" notification. Safe to call before
// the webview/app exists (it's used on the early-exit path when
// try_acquire_single_instance() returns false).
void notify_already_running();

} // namespace rui
