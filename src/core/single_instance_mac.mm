#include "core/single_instance.h"

#import <Cocoa/Cocoa.h>

namespace hwhelper {

// Native alert for the early-exit path. Runs before saucer creates its own
// application, so ensure the shared NSApplication exists first; NSAlert spins
// its own modal loop, so no running event loop is required.
void notify_already_running() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp activateIgnoringOtherApps:YES];

        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"Hardware Helper is already running.";
        alert.informativeText = @"Only one instance can run at a time. Switch to the window that's already open.";
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
    }
}

} // namespace hwhelper
