#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#define SOKOL_METAL
#include "sokol_gfx.h"

#include "yommd.hpp"

@interface AppDelegate: NSObject<NSApplicationDelegate>
@end

@interface Window: NSWindow
@end

@interface View: MTKView
@end

@interface ViewDelegate: NSObject<MTKViewDelegate>
@end

@interface AlertWindow : NSObject
-(void)showAlert:(NSString *)msg;
-(void)actionClose:(id)sender;
@end

@interface AlertWindowDelegate : NSObject<NSWindowDelegate>
@end

@interface AppMain: NSObject
-(void)createMainWindow;
-(void)createStatusItem;
-(void)actionQuit:(NSMenuItem *)sender;
-(void)actionToggleHandleMouse:(NSMenuItem *)sender;
-(void)actionResetModelPosition:(NSMenuItem *)sender;
-(const NSMenu *)getAppMenu;
-(sg_context_desc)getSokolContext;
-(id<CAMetalDrawable>)getDrawable;
-(MTLRenderPassDescriptor *)getRenderPassDescriptor;
-(NSSize)getWindowSize;
-(NSSize)getDrawableSize;
-(Routine&)getRoutine;
-(void)notifyInitializationDone;
-(bool)getInitialized;
@end

namespace{
const void *getSokolDrawable(void);
const void *getSokolRenderpassDescriptor(void);
inline AppMain *getAppMain(void);
}

@implementation AppDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    NSArray *argsArray = [[NSProcessInfo processInfo] arguments];
    std::vector<std::string> argsVec;
    for (NSString *arg in argsArray) {
        argsVec.push_back([arg UTF8String]);
    }
    const auto cmdArgs = CmdArgs::Parse(argsVec);

    auto appMain = getAppMain();
    [appMain createMainWindow];
    [appMain createStatusItem];

    auto& routine = [appMain getRoutine];
    routine.Init(cmdArgs);

    [appMain notifyInitializationDone];
}
- (void)applicationWillTerminate:(NSNotification *)aNotification {
    [getAppMain() getRoutine].Terminate();
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return YES;
}
@end

@implementation Window
- (void)mouseDragged:(NSEvent *)event {
    [getAppMain() getRoutine].OnMouseDragged();
}
- (void)mouseDown:(NSEvent *)event {
    [getAppMain() getRoutine].OnMouseDown();
}
- (void)scrollWheel:(NSEvent *)event {
    if (event.hasPreciseScrollingDeltas) {
        [getAppMain() getRoutine].OnWheelScrolled(event.scrollingDeltaY);
    }
}
- (BOOL)canBecomeKeyWindow {
    return NO;
}
@end

@implementation View
-(BOOL)acceptsFirstMouse:(NSEvent *)event {
    return NO;
}
- (const NSMenu *)menuForEvent:(NSEvent *)event {
    return [getAppMain() getAppMenu];
}
@end

@implementation ViewDelegate
- (void)mtkView:(nonnull MTKView*)view drawableSizeWillChange:(CGSize)size {
}
- (void)drawInMTKView:(nonnull MTKView*)view {
    if (![getAppMain() getInitialized])
        return;
    @autoreleasepool {
        auto& routine = [getAppMain() getRoutine];
        routine.Update();
        routine.Draw();
    }
}
@end

@implementation AppMain {
    AppDelegate *appDelegate_;
    Window *window_;
    View *view_;
    id<MTLDevice> metalDevice_;
    ViewDelegate *viewDelegate_;
    NSStatusItem *statusItem_;
    NSMenu *appMenu_;
    NSRunningApplication *alterApp_;
    Routine routine_;
    bool initialized_;
}
-(instancetype)init {
    self = [super init];
    if (self)
        initialized_ = false;
    return self;
}
-(void)createMainWindow {
    const NSUInteger style = NSWindowStyleMaskBorderless;
    // const NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;
    const auto screenRect = [NSScreen mainScreen].visibleFrame;
    const NSUInteger collectionBehavior =
        NSWindowCollectionBehaviorCanJoinAllSpaces |
        NSWindowCollectionBehaviorStationary |
        NSWindowCollectionBehaviorTransient |
        NSWindowCollectionBehaviorFullScreenAuxiliary |
        NSWindowCollectionBehaviorFullScreenDisallowsTiling |
        NSWindowCollectionBehaviorIgnoresCycle;

    window_ = [[Window alloc] initWithContentRect:screenRect
                                         styleMask:style
                                           backing:NSBackingStoreBuffered
                                             defer:NO];

    [window_ setTitle:@"yoMMD"];
    [window_ center];
    [window_ setIsVisible:YES];
    [window_ setOpaque:NO];
    [window_ setHasShadow:NO];
    [window_ setBackgroundColor:[NSColor clearColor]];
    [window_ setLevel:NSFloatingWindowLevel];
    [window_ setCollectionBehavior:collectionBehavior];
    [window_ setIgnoresMouseEvents:YES];

    viewDelegate_ = [[ViewDelegate alloc] init];
    metalDevice_ = MTLCreateSystemDefaultDevice();
    view_ = [[View alloc] init];
    [view_ setPreferredFramesPerSecond:static_cast<NSInteger>(Constant::FPS)];
    [view_ setDelegate:viewDelegate_];
    [view_ setDevice: metalDevice_];
    [view_ setColorPixelFormat:MTLPixelFormatBGRA8Unorm];
    [view_ setDepthStencilPixelFormat:MTLPixelFormatDepth32Float_Stencil8];
    // [view setAutoResizeDrawable:NO];

    [window_ setContentView:view_];
    [[view_ layer] setMagnificationFilter:kCAFilterNearest];
    [[view_ layer] setOpaque:NO];  // Make transparent
}
-(void)createStatusItem {
    const auto iconData = Resource::getStatusIconData();
    NSData *nsIconData = [NSData dataWithBytes:iconData.data()
                                        length:iconData.length()];
    NSImage *icon = [[NSImage alloc] initWithData:nsIconData];
    statusItem_ =
        [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
    [statusItem_.button setImage:icon];
    [statusItem_ setBehavior:NSStatusItemBehaviorTerminationOnRemoval];
    [statusItem_ setVisible:YES];

    appMenu_ = [[NSMenu alloc] init];

    NSMenuItem *enableMouse = [[NSMenuItem alloc]
                        initWithTitle:@"Enable Mouse"
                               action:@selector(actionToggleHandleMouse:)
                        keyEquivalent:@""];
    [enableMouse setTarget:self];
    [enableMouse setState:NSControlStateValueOff];


    NSMenuItem *resetModelPosition = [[NSMenuItem alloc]
                        initWithTitle:@"Reset Position"
                               action:@selector(actionResetModelPosition:)
                        keyEquivalent:@""];
    [resetModelPosition setTarget:self];

    NSMenuItem *quit = [[NSMenuItem alloc]
            initWithTitle:@"Quit" action:@selector(actionQuit:) keyEquivalent:@""];
    [quit setTarget:self];

    [appMenu_ addItem:enableMouse];
    [appMenu_ addItem:resetModelPosition];
    [appMenu_ addItem:[NSMenuItem separatorItem]];
    [appMenu_ addItem:quit];
    [statusItem_ setMenu:appMenu_];

    alterApp_ = NULL;
}
-(void)actionQuit:(id)sender {
    [NSApp terminate:sender];
}
-(void)actionToggleHandleMouse:(NSMenuItem *)sender {
    if (sender.state == NSControlStateValueOff) {
        NSArray *appList = [[NSWorkspace sharedWorkspace] runningApplications];
        for (NSRunningApplication *app in appList) {
            if (app.active) {
                alterApp_ = app;
                break;
            }
        }

        [sender setState:NSControlStateValueOn];
        [window_ setIgnoresMouseEvents:NO];
    } else {
        [sender setState:NSControlStateValueOff];
        [window_ setIgnoresMouseEvents:YES];
        if (NSApp.active && alterApp_) {
            [alterApp_ activateWithOptions:NSApplicationActivateIgnoringOtherApps];
        }
        alterApp_ = NULL;
    }
}
-(void)actionResetModelPosition:(NSMenuItem *)sender {
    routine_.ResetModelPosition();
}
-(const NSMenu *)getAppMenu {
    return appMenu_;
}
-(sg_context_desc)getSokolContext {
    return sg_context_desc{
        .sample_count = Constant::SampleCount,
        .metal = {
            .device = (__bridge const void *)metalDevice_,
            .renderpass_descriptor_cb = getSokolRenderpassDescriptor,
            .drawable_cb = getSokolDrawable,
        },
    };
}
-(id<CAMetalDrawable>)getDrawable {
    return [view_ currentDrawable];
}
-(MTLRenderPassDescriptor *)getRenderPassDescriptor {
    return [view_ currentRenderPassDescriptor];
}
-(NSSize)getWindowSize {
    return window_.frame.size;
}
-(NSSize)getDrawableSize {
    return view_.drawableSize;
}
-(Routine&)getRoutine {
    return routine_;
}
-(void)notifyInitializationDone {
    initialized_ = true;
}
-(bool)getInitialized {
    return initialized_;
}
@end

@implementation AlertWindow {
    NSWindow *window_;
}
static constexpr float paddingX_ = 10.0;
static constexpr float paddingY_ = 5.0;
-(void)showAlert:(NSString *)msg {
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
-(void)actionClose:(id)sender {
    [window_ close];
}
-(NSButton *)createButton {
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
-(NSScrollView *)createAlertView:(NSString *)msg frame:(NSRect)frame {
    NSFont *font = [NSFont monospacedSystemFontOfSize:12.0 weight:NSFontWeightRegular];
    NSTextStorage *textStorage = [[NSTextStorage alloc] initWithString:msg];
    NSTextContainer *container = [[NSTextContainer alloc] initWithContainerSize: NSMakeSize(FLT_MAX, FLT_MAX)];
    NSLayoutManager *layoutManager = [[NSLayoutManager alloc] init];

    [layoutManager addTextContainer:container];
    [textStorage addLayoutManager:layoutManager];
    [textStorage setFont:font];

    // Render glyph
    [layoutManager glyphRangeForTextContainer:container];

    // Get text bounds
    NSRect bounds = [layoutManager usedRectForTextContainer:container];

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
-(void)windowWillClose:(NSNotification *)notification {
    (void)notification;
    // Stop modal event loop for the alert window.
    [NSApp stopModal];
}
@end

namespace{
const void *getSokolDrawable() {
    return (__bridge const void *)[getAppMain() getDrawable];
}

const void *getSokolRenderpassDescriptor(void) {
    return (__bridge const void *)[getAppMain() getRenderPassDescriptor];
}

AppMain *getAppMain(void) {
    static AppMain *appMain = [[AppMain alloc] init];
    return appMain;
}
} // namespace

sg_context_desc Context::getSokolContext() {
    return [getAppMain() getSokolContext];
}

glm::vec2 Context::getWindowSize() {
    const auto size = [getAppMain() getWindowSize];
    return glm::vec2(size.width, size.height);
}

glm::vec2 Context::getDrawableSize() {
    const auto size = [getAppMain() getDrawableSize];
    return glm::vec2(size.width, size.height);
}

glm::vec2 Context::getMousePosition() {
    const auto pos = [NSEvent mouseLocation];
    return glm::vec2(pos.x, pos.y);
}

namespace Dialog {
void messageBox(std::string_view msg) {
    static AlertWindow *window;

    window = [AlertWindow alloc];
    [window showAlert:[NSString stringWithUTF8String:msg.data()]];
}
}

int main() {
    [NSApplication sharedApplication];

    auto appDelegate = [[AppDelegate alloc] init];

    [NSApp setDelegate:appDelegate];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [NSApp activateIgnoringOtherApps:NO];
    [NSApp run];
}
