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
    Info::Log("drawableSize:", size.width, ',', size.height);
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
        auto appList = [[NSWorkspace sharedWorkspace] runningApplications];
        for (NSUInteger i = 0; i < appList.count; ++i) {
            auto app = [appList objectAtIndex:i];
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
    // TODO: Implement more rich one.
    NSFont *font = [NSFont monospacedSystemFontOfSize:12.0 weight:NSFontWeightRegular];
    NSAlert *alert = [[NSAlert alloc] init];

    // Get all views present on the NSAlert content view. 5th element will be a
    // NSTextField object holding the Message text.
    NSArray* views = [[[alert window] contentView] subviews];
    NSTextField *text = (NSTextField *)[views objectAtIndex:5];
    [text setFont:font];
    [text setAlignment:NSTextAlignmentLeft];
    [[views objectAtIndex:4] setAlignment:NSTextAlignmentLeft];

    [alert setMessageText:@"yoMMD Error"];
    [alert setInformativeText:[NSString stringWithUTF8String:msg.data()]];
    [alert runModal];
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
