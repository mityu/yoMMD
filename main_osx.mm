#define SOKOL_METAL
#include "sokol_gfx.h"

#include "main.hpp"
#include "viewer.hpp"
#include "constant.hpp"
#include "util.hpp"
#include "keyboard.hpp"

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include <array>
#include <utility>
#include <functional>
#include <type_traits>

class GestureController {
public:
    GestureController();

    void SkipThisGesture();

    // Run "worker" unless this gesture should be skipped.
    template <typename T>
    void Emit(NSEvent *event, std::function<void()> worker, const T& cancelKeys);
public:
    static constexpr std::array<Keycode, 0> WontCancel{};
private:
    bool shouldSkip_;  // TRUE while gesture should be skipped
    std::array<bool, static_cast<std::size_t>(Keycode::Count)> prevKeyState_;
};

@interface AppDelegate: NSObject<NSApplicationDelegate>
@end

@interface Window: NSWindow
@end

@interface WindowDelegate : NSObject<NSWindowDelegate>
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
-(void)setIgnoreMouse:(bool)enable;
-(bool)getIgnoreMouse;  // Returns TRUE if this application ignores mouse events.
-(void)changeWindowScreen:(NSUInteger)scID;
-(NSMenu *)getAppMenu;
-(sg_environment)getSokolEnvironment;
-(sg_swapchain)getSokolSwapchain;
-(id<CAMetalDrawable>)getDrawable;
-(MTLRenderPassDescriptor *)getRenderPassDescriptor;
-(NSSize)getWindowSize;
-(NSPoint)getWindowPosition;
-(NSSize)getDrawableSize;
-(NSNumber *)getCurrentScreenNumber;
-(bool)isMenuOpened;
-(Routine&)getRoutine;
-(void)startDrawingModel;
@end

@interface AppMenuDelegate : NSObject<NSMenuDelegate>
-(bool)isMenuOpened;
-(void)actionQuit:(NSMenuItem *)sender;
-(void)actionEnableMouse:(NSMenuItem *)sender;
-(void)actionDisableMouse:(NSMenuItem *)sender;
-(void)actionHideWindow:(NSMenuItem *)sender;
-(void)actionShowWindow:(NSMenuItem *)sender;
-(void)actionResetModelPosition:(NSMenuItem *)sender;
-(void)actionChangeScreen:(NSMenuItem *)sender;
@end

@interface SelectScreenMenuDelegate : NSObject<NSMenuDelegate>
@end

namespace{
inline AppMain *getAppMain(void);

// Get currently active application.  If it's not found, returns NULL.
NSRunningApplication *getActiveApplication(void);

// Find NSScreen from NSScreenNumber.  If screen is not found, returns nil.
inline NSScreen *findScreenFromID(NSInteger scID);
}

GestureController::GestureController() :
    shouldSkip_(false)
{}

void GestureController::SkipThisGesture() {
    shouldSkip_ = true;
}

template <typename T>
void GestureController::Emit(NSEvent *event, std::function<void()> worker, const T& cancelKeys) {
    static_assert(
                std::is_same_v<typename T::value_type, Keycode>,
                "Contained value type must be Keycode.");
    if (shouldSkip_) {
        if (event.phase == NSEventPhaseBegan)
            // Switched to a new gesture.  Cancel skipping gesture.
            shouldSkip_ = false;
        else
            return;
    }
    if (event.phase == NSEventPhaseBegan) {
        for (auto key : cancelKeys) {
            prevKeyState_[static_cast<std::size_t>(key)] = Keyboard::IsKeyPressed(key);
        }
    }
    for (auto key : cancelKeys) {
        if (prevKeyState_[static_cast<std::size_t>(key)] != Keyboard::IsKeyPressed(key)) {
            SkipThisGesture();
            return;
        }
    }
    worker();
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
    auto& routine = [appMain getRoutine];
    routine.ParseConfig(cmdArgs);
    [appMain createMainWindow];
    [appMain createStatusItem];

    routine.Init();

    [appMain startDrawingModel];
}
- (void)applicationWillTerminate:(NSNotification *)aNotification {
    [getAppMain() getRoutine].Terminate();
}
@end

@implementation Window {
    GestureController gestureController_;
}
- (void)flagsChanged:(NSEvent *)event {
    using KeycodeMap = std::pair<NSEventModifierFlags, Keycode>;
    constexpr std::array<KeycodeMap, static_cast<size_t>(Keycode::Count)> keys({{
            {NSEventModifierFlagShift, Keycode::Shift},
    }});
    for (const auto& [mask, keycode] : keys) {
        if (event.modifierFlags & mask)
            Keyboard::OnKeyDown(keycode);
        else
            Keyboard::OnKeyUp(keycode);
    }
}
- (void)mouseDragged:(NSEvent *)event {
    [getAppMain() getRoutine].OnMouseDragged();
}
- (void)mouseDown:(NSEvent *)event {
    [getAppMain() getRoutine].OnGestureBegin();
}
- (void)mouseUp:(NSEvent *)event {
    [getAppMain() getRoutine].OnGestureEnd();
}
- (void)scrollWheel:(NSEvent *)event {
    constexpr std::array<Keycode, 1> cancelKeys = {
        Keycode::Shift
    };
    float delta = event.deltaY * 10.0f;  // TODO: Better factor
    if (event.hasPreciseScrollingDeltas)
        delta = event.scrollingDeltaY;

    if (!event.directionInvertedFromDevice)
        delta = -delta;

    const auto worker = [delta](){[getAppMain() getRoutine].OnWheelScrolled(delta);};
    gestureController_.Emit(event, worker, cancelKeys);
}
-(void)magnifyWithEvent:(NSEvent *)event {
    // NOTE: It seems touchpad gesture events aren't dispatched when
    // application isn't active.  Do try activate appliction when touchpad
    // gesture doesn't work.
    auto& routine = [getAppMain() getRoutine];
    GesturePhase phase = GesturePhase::Unknown;
    switch (event.phase) {
    case NSEventPhaseMayBegin:  // fall-through
    case NSEventPhaseBegan:
        routine.OnGestureBegin();
        phase = GesturePhase::Begin;
        break;
    case NSEventPhaseChanged:
        phase = GesturePhase::Ongoing;
        break;
    case NSEventPhaseEnded:  // fall-through
    case NSEventPhaseCancelled:
        routine.OnGestureEnd();
        phase = GesturePhase::End;
        break;
    }
    const auto worker = [&phase, &event](){
        [getAppMain() getRoutine].OnGestureZoom(phase, event.magnification);
    };
    gestureController_.Emit(event, worker, GestureController::WontCancel);
}
-(void)smartMagnifyWithEvent:(NSEvent *)event {
    Routine& routine = [getAppMain() getRoutine];
    const float defaultScale = routine.GetConfig().defaultScale;
    const float scale = routine.GetModelScale();
    float delta = 0.6f;
    GesturePhase phase = GesturePhase::Begin;
    if (scale != defaultScale) {
        // Reset scaling
        delta = defaultScale - scale;
        phase = GesturePhase::Ongoing;
    }
    routine.OnGestureBegin();
    routine.OnGestureZoom(phase, delta);
    routine.OnGestureEnd();
}
- (BOOL)canBecomeKeyWindow {
    // Return YES here to enable touch gesture events; by default, they're
    // disabled for boderless windows.
    return YES;
}
@end

@implementation WindowDelegate
-(void)windowDidChangeScreen:(NSNotification *)notification {
    NSWindow *window = [notification object];
    NSScreen *screen = [window screen];
    if (!NSEqualRects(window.frame, screen.visibleFrame)) {
        [window setFrame:screen.visibleFrame display:YES animate:NO];
    }
}
-(void)windowWillClose:(NSNotification *)notification {
    [NSApp terminate:self];
}
@end

@implementation View
+ (NSMenu *)defaultMenu {
    return [getAppMain() getAppMenu];
}
-(BOOL)acceptsFirstMouse:(NSEvent *)event {
    return NO;
}
@end

@implementation ViewDelegate
- (void)mtkView:(nonnull MTKView*)view drawableSizeWillChange:(CGSize)size {
}
- (void)drawInMTKView:(nonnull MTKView*)view {
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
    WindowDelegate *windowDelegate_;
    View *view_;
    id<MTLDevice> metalDevice_;
    ViewDelegate *viewDelegate_;
    NSStatusItem *statusItem_;
    NSMenu *appMenu_;
    AppMenuDelegate *appMenuDelegate_;
    SelectScreenMenuDelegate *selectScreenMenuDelegate_;
    Routine routine_;
}
-(void)createMainWindow {
    const NSUInteger style = NSWindowStyleMaskBorderless;
    // const NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;
    const auto screenRect = []() {
        const auto& screenNumber = [getAppMain() getRoutine].GetConfig().defaultScreenNumber;
        if (screenNumber.has_value()) {
            NSScreen *screen = findScreenFromID(*screenNumber);
            if (screen)
                return screen.visibleFrame;
        }
        return [NSScreen mainScreen].visibleFrame;
    }();
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
    windowDelegate_ = [[WindowDelegate alloc] init];

    [window_ setDelegate:windowDelegate_];
    [window_ setTitle:@"yoMMD"];
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
    [view_ setDevice: metalDevice_];
    [view_ setColorPixelFormat:MTLPixelFormatBGRA8Unorm];
    [view_ setDepthStencilPixelFormat:MTLPixelFormatDepth32Float_Stencil8];
    [view_ setSampleCount:static_cast<NSUInteger>(Constant::PreferredSampleCount)];
    // Postpone setting view delegate until every initialization process is
    // done in order to prohibit MMD drawer to draw models while showing errors
    // due to failure of loading MMD models (e.g. MMD model path is given, but
    // the path is invalid).

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

    appMenuDelegate_ = [[AppMenuDelegate alloc] init];
    appMenu_ = [[NSMenu alloc] init];
    [appMenu_ setDelegate:appMenuDelegate_];
    [statusItem_ setMenu:appMenu_];
}
-(void)setIgnoreMouse:(bool)enable {
    [window_ setIgnoresMouseEvents:enable];
    if (!enable)
        Keyboard::ResetAllState();
}
-(bool)getIgnoreMouse {
    return [window_ ignoresMouseEvents];
}
-(void)changeWindowScreen:(NSUInteger)scID {
    NSScreen *dst = findScreenFromID(scID);
    if (!dst) {
        // Display not found.  Maybe the connection for the target display is
        // removed.
        Info::Log("Display not found: %ld", scID);
        return;
    }
    [window_ setFrame:dst.visibleFrame display:YES animate:NO];
}
-(NSMenu *)getAppMenu {
    // NOTE: Appropriating "appMenu_" here will break status menu after using
    // right click menu.  (Status menu will disappear after a use of right
    // click menu.)  As a workaround, make a new NSMenu object and use it.
    NSMenu * menu = [[NSMenu alloc] init];
    [menu setDelegate:appMenuDelegate_];
    return menu;
}
-(sg_environment)getSokolEnvironment {
    return sg_environment {
        .defaults = {
            .color_format = SG_PIXELFORMAT_BGRA8,
            .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
            .sample_count = Constant::PreferredSampleCount
        },
        .metal = {
            .device = (__bridge const void *)metalDevice_,
        },
    };
}
-(sg_swapchain)getSokolSwapchain {
    const auto size{Context::getWindowSize()};
    return sg_swapchain {
        .width = static_cast<int>(size.x),
        .height= static_cast<int>(size.y),
        .sample_count = Constant::PreferredSampleCount,
        .color_format = SG_PIXELFORMAT_BGRA8,
        .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
        .metal = {
            .current_drawable = (__bridge const void *)[view_ currentDrawable],
            .depth_stencil_texture = (__bridge const void *)[view_ depthStencilTexture],
            .msaa_color_texture = (__bridge const void *)[view_ multisampleColorTexture],
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
-(NSPoint)getWindowPosition {
    return window_.frame.origin;
}
-(NSSize)getDrawableSize {
    return view_.drawableSize;
}
-(NSNumber *)getCurrentScreenNumber {
    NSScreen *screen = [window_ screen];
    if (!screen)
        Err::Log("Internal error? screen is offscreen");
    return [screen deviceDescription][@"NSScreenNumber"];
}
-(bool)isMenuOpened {
    return [appMenuDelegate_ isMenuOpened];
}
-(Routine&)getRoutine {
    return routine_;
}
-(void)startDrawingModel {
    [view_ setDelegate:viewDelegate_];
}
@end

@implementation AppMenuDelegate {
    NSArray<NSMenuItem *> *menuItems_;
    SelectScreenMenuDelegate *selectScreenMenuDelegate_;
    NSRunningApplication *alterApp_;
    bool isMenuOpened_;
}
enum class MenuTag : NSInteger {
    None,
    EnableMouse,
    SelectScreen,
    HideWindow,
};
-(instancetype)init {
    self = [super init];
    if (self) {
        alterApp_ = nil;
        isMenuOpened_ = false;

        // Menu title and action is is set later in menuNeedsUpdate.
        NSMenuItem *enableMouse = [[NSMenuItem alloc]
                            initWithTitle:@""
                                   action:nil
                            keyEquivalent:@""];
        [enableMouse setTag:Enum::underlyCast(MenuTag::EnableMouse)];
        [enableMouse setTarget:self];


        NSMenuItem *resetModelPosition = [[NSMenuItem alloc]
                            initWithTitle:@"Reset Position"
                                   action:@selector(actionResetModelPosition:)
                            keyEquivalent:@""];
        [resetModelPosition setTag:Enum::underlyCast(MenuTag::None)];
        [resetModelPosition setTarget:self];

        selectScreenMenuDelegate_ = [[SelectScreenMenuDelegate alloc] init];
        NSMenuItem *selectScreen = [[NSMenuItem alloc]
                            initWithTitle:@"Select screen"
                                   action:nil
                            keyEquivalent:@""];
        [selectScreen setTag:Enum::underlyCast(MenuTag::SelectScreen)];
        [selectScreen setSubmenu:[[NSMenu alloc] init]];
        [selectScreen.submenu setDelegate:selectScreenMenuDelegate_];
        // In order to be able to disable menu items manually.
        [selectScreen.submenu setAutoenablesItems:NO];
        [selectScreen setTarget:self];

        // Menu title and action is is set later in menuNeedsUpdate.
        NSMenuItem *hideWindow = [[NSMenuItem alloc]
                        initWithTitle:@""
                               action:nil
                        keyEquivalent:@""];
        [hideWindow setTag:Enum::underlyCast(MenuTag::HideWindow)];
        [hideWindow setTarget:self];

        NSMenuItem *quit = [[NSMenuItem alloc]
                initWithTitle:@"Quit" action:@selector(actionQuit:) keyEquivalent:@""];
        [quit setTag:Enum::underlyCast(MenuTag::None)];
        [quit setTarget:self];

        menuItems_ = @[
            enableMouse,
            resetModelPosition,
            [NSMenuItem separatorItem],
            selectScreen,
            [NSMenuItem separatorItem],
            hideWindow,
            [NSMenuItem separatorItem],
            quit,
        ];
    }
    return self;
}
-(void)menu:(NSMenu *)menu willHighlightItem:(NSMenuItem *)item {
    if (!item || item.tag != Enum::underlyCast(MenuTag::SelectScreen))
        return;

    NSNumber *currentScreenID = [getAppMain() getCurrentScreenNumber];

    NSMenu *subMenu = item.submenu;
    [subMenu removeAllItems];
    for (NSScreen *screen in [NSScreen screens]) {
        NSNumber *scID = [screen deviceDescription][@"NSScreenNumber"];
        NSString *title = [[NSString alloc]
                    initWithFormat:@"Screen%@", [scID stringValue]];
        NSMenuItem *subItem = [[NSMenuItem alloc]
                        initWithTitle:title
                               action:@selector(actionChangeScreen:)
                        keyEquivalent:@""];
        [subItem setTag:[scID integerValue]];
        [subItem setTarget:self];
        if ([scID isEqualToNumber:currentScreenID]) {
            [subItem setEnabled:NO];
        }
        [subMenu addItem:subItem];
    }
}
-(void)menuNeedsUpdate:(NSMenu *)menu {
    if ([menu numberOfItems] != static_cast<NSInteger>([menuItems_ count])) {
        [menu removeAllItems];  // Initialize menu
        [menu setItemArray:[[NSArray alloc] initWithArray:menuItems_ copyItems:YES]];
    }

    NSMenuItem *enableMouse = [menu itemWithTag:Enum::underlyCast(MenuTag::EnableMouse)];
    if (!enableMouse) {
        Err::Log("Internal error: \"Enable Mouse\" or \"Disable Mouse\" menu not found");
        return;
    }

    if ([getAppMain() getIgnoreMouse]) {
        [enableMouse setTitle:@"Enable Mouse"];
        [enableMouse setAction:@selector(actionEnableMouse:)];
    } else {
        [enableMouse setTitle:@"Disable Mouse"];
        [enableMouse setAction:@selector(actionDisableMouse:)];
    }

    NSMenuItem *hideWindow = [menu itemWithTag:Enum::underlyCast(MenuTag::HideWindow)];
    if (!hideWindow) {
        Err::Log("Internal error: \"Hide Window\" or \"Show Window\" menu not found");
        return;
    }

    if (NSApp.hidden) {
        [hideWindow setTitle:@"Show Window"];
        [hideWindow setAction:@selector(actionShowWindow:)];
    } else {
        [hideWindow setTitle:@"Hide Window"];
        [hideWindow setAction:@selector(actionHideWindow:)];
    }

    NSMenuItem *selectScreen = [menu itemWithTag:Enum::underlyCast(MenuTag::SelectScreen)];
    if (!selectScreen) {
        Err::Log("Internal error: \"Select screen\" menu not found");
        return;
    }

    if ([[NSScreen screens] count] <= 1) {
        [selectScreen setEnabled:NO];
    } else {
        [selectScreen setEnabled:YES];
    }
}
-(void)menuWillOpen:(NSMenu *)menu {
    isMenuOpened_ = true;
}
-(void)menuDidClose:(NSMenu *)menu {
    isMenuOpened_ = false;
}
-(bool)isMenuOpened {
    return isMenuOpened_;
}
-(void)actionQuit:(id)sender {
    [NSApp terminate:sender];
}
-(void)actionEnableMouse:(NSMenuItem *)sender {
    alterApp_ = getActiveApplication();

    [getAppMain() setIgnoreMouse:false];

    // Activate this app to enable touchpad gesture.
    [NSApp activateIgnoringOtherApps:YES];
}
-(void)actionDisableMouse:(NSMenuItem *)sender {
    [getAppMain() setIgnoreMouse:true];
    if (NSApp.active && alterApp_) {
        [alterApp_ activateWithOptions:NSApplicationActivateIgnoringOtherApps];
    }
    alterApp_ = NULL;
}
-(void)actionHideWindow:(NSMenuItem *)sender {
    [NSApp hide:self];
}
-(void)actionShowWindow:(NSMenuItem *)sender {
    NSRunningApplication *activeApp = getActiveApplication();
    [NSApp unhide:self];
    if ([getAppMain() getIgnoreMouse]) {
        if (NSApp.active && activeApp) {
            [activeApp activateWithOptions:NSApplicationActivateIgnoringOtherApps];
        }
    } else if (!NSApp.active) {
        [NSApp activateIgnoringOtherApps:YES];
    }
}
-(void)actionResetModelPosition:(NSMenuItem *)sender {
    [getAppMain() getRoutine].ResetModelPosition();
}
-(void)actionChangeScreen:(NSMenuItem *)sender {
    [getAppMain() changeWindowScreen:sender.tag];
}
@end

@implementation SelectScreenMenuDelegate {
    NSWindow *window_;
}
-(instancetype)init {
    self = [super init];
    if (self) {
        constexpr NSUInteger style = NSWindowStyleMaskBorderless;
        constexpr NSUInteger collectionBehavior =
            NSWindowCollectionBehaviorCanJoinAllSpaces |
            NSWindowCollectionBehaviorStationary |
            NSWindowCollectionBehaviorTransient |
            NSWindowCollectionBehaviorFullScreenAuxiliary |
            NSWindowCollectionBehaviorFullScreenDisallowsTiling |
            NSWindowCollectionBehaviorIgnoresCycle;
        NSColor *bgColor = [NSColor colorWithDeviceRed:0.0 green:0.0 blue:0.0 alpha:0.5];
        window_ = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 0, 0)
                                              styleMask:style
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
        [window_ setCollectionBehavior:collectionBehavior];
        [window_ setLevel:NSFloatingWindowLevel];
        [window_ setHasShadow:NO];
        [window_ setOpaque:NO];
        [window_ setBackgroundColor:bgColor];
    }
    return self;
}
-(void)menuDidClose:(NSMenu *)menu {
    [window_ setIsVisible:NO];
    [window_ setFrame:NSMakeRect(0, 0, 0, 0) display:NO];
}
-(void)menu:(NSMenu *)menu willHighlightItem:(NSMenuItem *)item {
    if (!item) {
        [window_ setIsVisible:NO];
        return;
    }

    NSScreen *dst = findScreenFromID(item.tag);
    if (!dst) {
        // Display not found.  Maybe the connection for the target display is
        // removed.
        Info::Log("Display not found: %ld", item.tag);
        return;
    }
    [window_ setFrame:dst.visibleFrame display:YES animate:NO];
    [window_ setIsVisible:YES];
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
AppMain *getAppMain(void) {
    static AppMain *appMain = [[AppMain alloc] init];
    return appMain;
}

NSRunningApplication *getActiveApplication(void) {
    NSArray *appList = [[NSWorkspace sharedWorkspace] runningApplications];
    for (NSRunningApplication *app in appList) {
        if (app.active) {
            return app;
        }
    }
    return NULL;
}

NSScreen *findScreenFromID(NSInteger scID) {
    NSNumber *target = [[NSNumber alloc] initWithInteger:scID];
    for (NSScreen *sc in [NSScreen screens]) {
        NSNumber *scNum = [sc deviceDescription][@"NSScreenNumber"];
        if ([scNum isEqualToNumber:target]) {
            return sc;
        }
    }
    return nil;
}
} // namespace

sg_environment Context::getSokolEnvironment() {
    return [getAppMain() getSokolEnvironment];
}

sg_swapchain Context::getSokolSwapchain() {
    return [getAppMain() getSokolSwapchain];
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
    const auto origin = [getAppMain() getWindowPosition];
    return glm::vec2(pos.x - origin.x, pos.y - origin.y);
}

int Context::getSampleCount() {
    return Constant::PreferredSampleCount;
}

bool Context::shouldEmphasizeModel() {
    return [getAppMain() isMenuOpened];
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
