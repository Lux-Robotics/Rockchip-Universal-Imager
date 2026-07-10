#include "core/macos_quit_guard.h"

#import <Cocoa/Cocoa.h>

namespace hwhelper {
namespace {

std::function<bool()> g_should_block;
std::function<void()> g_on_blocked;

} // namespace
} // namespace hwhelper

@interface HWHelperQuitGuard : NSObject <NSApplicationDelegate>
@end

@implementation HWHelperQuitGuard

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    if (hwhelper::g_should_block && hwhelper::g_should_block()) {
        if (hwhelper::g_on_blocked) {
            hwhelper::g_on_blocked();
        }
        return NSTerminateCancel;
    }
    return NSTerminateNow;
}

@end

namespace hwhelper {

void install_quit_guard(std::function<bool()> should_block, std::function<void()> on_blocked) {
    g_should_block = std::move(should_block);
    g_on_blocked = std::move(on_blocked);

    // saucer never sets an NSApplicationDelegate itself (only a per-window
    // delegate for windowShouldClose etc.), so this doesn't step on
    // anything - but it does mean nothing else can install one either.
    static HWHelperQuitGuard* guard = [[HWHelperQuitGuard alloc] init];
    [NSApp setDelegate:guard];
}

} // namespace hwhelper
