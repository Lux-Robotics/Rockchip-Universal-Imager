#include "core/file_drop.h"

#include <saucer/window.hpp>
#include <saucer/modules/stable/webkit.hpp>

#import <Cocoa/Cocoa.h>

// A transparent view layered over the whole content view that participates
// only in drag-and-drop. AppKit routes drag sessions to the frontmost view
// under the pointer that is registered for the dragged type - being above
// the WKWebView, this view wins, which is what stops WebKit's default "drop
// a file to navigate to it" behavior for .img files. hitTest: returns nil so
// every ordinary mouse event still falls through to the webview beneath.
@interface HWHelperFileDropView : NSView {
  @public
    std::function<void(const std::string&)> on_drop_;
}
@end

namespace {

// The single .img file URL of a drag session, or nil if the session isn't
// exactly that (multiple files, non-file content, other extensions).
NSURL* imgFileUrl(id<NSDraggingInfo> sender) {
    NSDictionary* options = @{
        NSPasteboardURLReadingFileURLsOnlyKey : @YES,
    };
    NSArray<NSURL*>* urls = [sender.draggingPasteboard readObjectsForClasses:@[ [NSURL class] ]
                                                                     options:options];
    if (urls.count != 1) {
        return nil;
    }
    NSURL* url = urls.firstObject;
    if (![url.pathExtension.lowercaseString isEqualToString:@"img"]) {
        return nil;
    }
    return url;
}

} // namespace

@implementation HWHelperFileDropView

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    return imgFileUrl(sender) != nil ? NSDragOperationCopy : NSDragOperationNone;
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
    return imgFileUrl(sender) != nil ? NSDragOperationCopy : NSDragOperationNone;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSURL* url = imgFileUrl(sender);
    if (url == nil) {
        return NO;
    }
    if (on_drop_) {
        on_drop_(std::string(url.fileSystemRepresentation));
    }
    return YES;
}

- (NSView*)hitTest:(NSPoint)point {
    return nil; // drag-and-drop only; never intercept mouse events
}

@end

namespace rui {

void install_file_drop_target(const std::shared_ptr<saucer::window>& window,
                              std::function<void(const std::string&)> on_drop) {
    NSWindow* ns_window = window->native<true>().window;
    NSView* content = ns_window.contentView;
    if (content == nil) {
        return;
    }

    HWHelperFileDropView* drop_view = [[HWHelperFileDropView alloc] initWithFrame:content.bounds];
    drop_view->on_drop_ = std::move(on_drop);
    drop_view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [drop_view registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];

    [content addSubview:drop_view positioned:NSWindowAbove relativeTo:nil];
}

} // namespace rui
