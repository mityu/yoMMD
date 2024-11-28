#include "alert_window.hpp"

@interface AlertWindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation AlertWindow {
    NSWindow *window_;
}
static constexpr float paddingX_ = 10.0;
static constexpr float paddingY_ = 5.0;
- (void)showAlert:(NSString *)msg {
    NSScrollView *alertView_;
    NSButton *button_;

    window_ = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 600, 350)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [window_ setTitle:@"yoMMD Error"];
    [window_ setIsVisible:YES];
    [window_ center];
    [window_ setDelegate:[[AlertWindowDelegate alloc] init]];

    button_ = [self createButton];

    NSRect logViewFrame = window_.contentView.frame;
    logViewFrame.origin.x += paddingX_;
    logViewFrame.origin.y += paddingY_ * 2 + button_.frame.size.height;
    logViewFrame.size.width -= paddingX_ * 2;
    logViewFrame.size.height -= paddingY_ * 4 + button_.frame.size.height;

    alertView_ = [self createAlertView:msg frame:logViewFrame];

    [window_.contentView addSubview:alertView_];
    [window_.contentView addSubview:button_];
    [window_ setDefaultButtonCell:button_.cell];

    if (!NSApp.active)
        [NSApp activateIgnoringOtherApps:YES];
    [window_ makeKeyAndOrderFront:window_];

    // Start modal event loop for the alert window.  This modal event loop
    // should be ended on "windowWillClose" callback.
    [NSApp runModalForWindow:window_];
}
- (void)actionClose:(id)sender {
    [window_ close];
}
- (NSButton *)createButton {
    NSButton *button = [NSButton buttonWithTitle:@"OK"
                                          target:self
                                          action:@selector(actionClose:)];
    const NSRect& viewFrame = window_.contentView.frame;
    NSSize size = button.frame.size;
    NSPoint origin = viewFrame.origin;
    size.width = size.width * 5 / 3;
    origin.x += viewFrame.size.width - size.width - paddingX_;
    origin.y += paddingY_;

    [button setFrameOrigin:origin];
    [button setFrameSize:size];

    return button;
}
- (NSScrollView *)createAlertView:(NSString *)msg frame:(NSRect)frame {
    NSFont *font = [NSFont monospacedSystemFontOfSize:12.0 weight:NSFontWeightRegular];
    NSTextStorage *textStorage = [[NSTextStorage alloc] initWithString:msg];
    NSTextContainer *container =
        [[NSTextContainer alloc] initWithContainerSize:NSMakeSize(FLT_MAX, FLT_MAX)];
    NSLayoutManager *layoutManager = [[NSLayoutManager alloc] init];

    [layoutManager addTextContainer:container];
    [textStorage addLayoutManager:layoutManager];
    [textStorage setFont:font];

    // Render glyph
    [layoutManager glyphRangeForTextContainer:container];

    // Get text bounds
    const NSRect bounds = [layoutManager usedRectForTextContainer:container];

    NSTextView *textView = [[NSTextView alloc] initWithFrame:bounds textContainer:container];
    [textView setEditable:NO];

    NSClipView *clipView = [[NSClipView alloc] initWithFrame:bounds];
    clipView.documentView = textView;

    NSScrollView *scrollView = [[NSScrollView alloc] initWithFrame:frame];
    [scrollView setContentView:clipView];
    [scrollView setHasHorizontalScroller:YES];
    [scrollView setHasVerticalScroller:YES];

    return scrollView;
}
@end

@implementation AlertWindowDelegate
- (void)windowWillClose:(NSNotification *)notification {
    (void)notification;
    // Stop modal event loop for the alert window.
    [NSApp stopModal];
}
@end
