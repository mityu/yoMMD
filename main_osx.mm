#include "platform.hpp"
#ifdef PLATFORM_MAC

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#define SOKOL_METAL
#include "sokol_gfx.h"

#include "yommd.hpp"

#define DISABLE_TRANSPARENT_WINDOW 0

@interface AppDelegate: NSObject<NSApplicationDelegate>
@end

@interface WindowDelegate: NSObject<NSWindowDelegate>
@end

@interface ViewDelegate: NSObject<MTKViewDelegate>
@end

@interface AppMain: NSObject
-(void)createMainWindow;
-(void)createStatusItem;
-(sg_context_desc)getSokolContext;
-(id<CAMetalDrawable>)getDrawable;
-(MTLRenderPassDescriptor *)getRenderPassDescriptor;
-(NSSize)getWindowSize;
-(Routine&)getRoutine;
@end

static AppMain *appMain;
static const void *getSokolDrawable(void);
static const void *getSokolRenderpassDescriptor(void);

@implementation AppDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    appMain = [[AppMain alloc] init];
    [appMain createMainWindow];
    [appMain createStatusItem];
    [appMain getRoutine].LoadMMD();
    [appMain getRoutine].Init();
}
- (void)applicationWillTerminate:(NSNotification *)aNotification {
    [appMain getRoutine].Terminate();
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return YES;
}
@end

@implementation WindowDelegate
@end

@implementation ViewDelegate
- (void)mtkView:(nonnull MTKView*)view drawableSizeWillChange:(CGSize)size {
}
- (void)drawInMTKView:(nonnull MTKView*)view {
    @autoreleasepool {
        auto& routine = [appMain getRoutine];
        routine.Update();
        routine.Draw();
    }
}
@end

@implementation AppMain {
    AppDelegate *appDelegate;
    NSWindow *window;
    WindowDelegate *windowDelegate;
    MTKView *view;
    id<MTLDevice> metalDevice;
    ViewDelegate *viewDelegate;
    Routine routine;
}
-(void)createMainWindow {
    // const NSUInteger style = NSWindowStyleMaskBorderless;
    const NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;
    const auto screenRect = [NSScreen mainScreen].visibleFrame;

    window = [[NSWindow alloc] initWithContentRect:screenRect
                                         styleMask:style
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
    windowDelegate = [[WindowDelegate alloc] init];

    [window setTitle:@"yoMMD"];
    [window center];
    [window setIsVisible:YES];
#if DISABLE_TRANSPARENT_WINDOW == 0
    [window setOpaque:NO];
    [window setHasShadow:NO];
    [window setBackgroundColor:[NSColor clearColor]];
#endif
    [window setLevel:NSFloatingWindowLevel];
    [window setDelegate:windowDelegate];
    // [window setIgnoresMouseEvents:YES];

    viewDelegate = [[ViewDelegate alloc] init];
    metalDevice = MTLCreateSystemDefaultDevice();
    view = [[MTKView alloc] init];
    [view setPreferredFramesPerSecond:static_cast<NSInteger>(constant::FPS)];
    [view setDelegate:viewDelegate];
    [view setDevice: metalDevice];
    [view setColorPixelFormat:MTLPixelFormatBGRA8Unorm];
    [view setDepthStencilPixelFormat:MTLPixelFormatDepth32Float_Stencil8];

    [window setContentView:view];
    [view setDrawableSize:screenRect.size];
    [[view layer] setMagnificationFilter:kCAFilterNearest];
#if DISABLE_TRANSPARENT_WINDOW == 0
    [[view layer] setOpaque:NO];  // Make transparent
#endif
}
-(void)createStatusItem {
    // TODO: implement
}
-(sg_context_desc)getSokolContext {
    return sg_context_desc{
        .sample_count = constant::SampleCount,  // TODO:
        .metal = {
            .device = (__bridge const void *)metalDevice,
            .renderpass_descriptor_cb = getSokolRenderpassDescriptor,
            .drawable_cb = getSokolDrawable,
        },
    };
}
-(id<CAMetalDrawable>)getDrawable {
    return [view currentDrawable];
}
-(MTLRenderPassDescriptor *)getRenderPassDescriptor {
    return [view currentRenderPassDescriptor];
}
-(NSSize)getWindowSize {
    return window.frame.size;
}
-(Routine&)getRoutine {
    return routine;
}
@end

static const void *getSokolDrawable() {
    return (__bridge const void *)[appMain getDrawable];
}

static const void *getSokolRenderpassDescriptor(void) {
    return (__bridge const void *)[appMain getRenderPassDescriptor];
}

sg_context_desc Context::getSokolContext() {
    return [appMain getSokolContext];
}

std::pair<int, int> Context::getWindowSize() {
    const auto size = [appMain getWindowSize];
    return {size.width, size.height};
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    [NSApplication sharedApplication];

    auto appDelegate = [[AppDelegate alloc] init];

    [NSApp setDelegate:appDelegate];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
    [NSApp run];
}

#endif  // PLATFORM_MAC
