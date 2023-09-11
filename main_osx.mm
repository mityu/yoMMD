#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#define SOKOL_METAL
#include "sokol_gfx.h"

#include "main.hpp"
#include "viewer.hpp"
#include "constant.hpp"
#include "util.hpp"

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
-(bool)getIgnoreMouse;
-(void)changeWindowScreen:(NSUInteger)scID;
-(NSMenu *)getAppMenu;
-(sg_context_desc)getSokolContext;
-(id<CAMetalDrawable>)getDrawable;
-(MTLRenderPassDescriptor *)getRenderPassDescriptor;
-(NSSize)getWindowSize;
-(NSPoint)getWindowPosition;
-(NSSize)getDrawableSize;
-(NSNumber *)getCurrentScreenNumber;
-(Routine&)getRoutine;
-(void)startDrawingModel;
@end

@interface AppMenuDelegate : NSObject<NSMenuDelegate>
-(void)actionQuit:(NSMenuItem *)sender;
-(void)actionToggleHandleMouse:(NSMenuItem *)sender;
-(void)actionResetModelPosition:(NSMenuItem *)sender;
-(void)actionChangeScreen:(NSMenuItem *)sender;
@end

@interface SelectScreenMenuDelegate : NSObject<NSMenuDelegate>
@end

namespace{
const void *getSokolDrawable(void);
const void *getSokolRenderpassDescriptor(void);
inline AppMain *getAppMain(void);

// Find NSScreen from NSScreenNumber.  If screen is not found, returns nil.
inline NSScreen *findScreenFromID(NSInteger scID);
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
    float delta = event.deltaY * 10.0f;  // TODO: Better factor
    if (event.hasPreciseScrollingDeltas)
        delta = event.scrollingDeltaY;

    if (!event.directionInvertedFromDevice)
        delta = -delta;

    [getAppMain() getRoutine].OnWheelScrolled(delta);
}
-(void)magnifyWithEvent:(NSEvent *)event {
    // NOTE: It seems touchpad gesture events aren't dispatched when
    // application isn't active.  Do try activate appliction when touchpad
    // gesture doesn't work.
    GesturePhase phase = GesturePhase::Unknown;
    switch (event.phase) {
    case NSEventPhaseMayBegin:  // fall-through
    case NSEventPhaseBegan:
        phase = GesturePhase::Begin;
        break;
    case NSEventPhaseChanged:
        phase = GesturePhase::Ongoing;
        break;
    case NSEventPhaseEnded:  // fall-through
    case NSEventPhaseCancelled:
        phase = GesturePhase::End;
        break;
    }
    [getAppMain() getRoutine].OnGestureZoom(phase, event.magnification);
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
    routine.OnGestureZoom(phase, delta);
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
}
enum class MenuTag : NSInteger {
    None,
    EnableMouse,
    SelectScreen,
};
-(instancetype)init {
    self = [super init];
    if (self) {
        alterApp_ = nil;

        NSMenuItem *enableMouse = [[NSMenuItem alloc]
                            initWithTitle:@"Enable Mouse"
                                   action:@selector(actionToggleHandleMouse:)
                            keyEquivalent:@""];
        [enableMouse setTag:Enum::underlyCast(MenuTag::EnableMouse)];
        [enableMouse setTarget:self];
        [enableMouse setState:NSControlStateValueOff];


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
        Err::Log("Internal error: \"Enable Mouse\" menu not found");
        return;
    }

    if ([getAppMain() getIgnoreMouse])
        [enableMouse setState:NSControlStateValueOff];
    else
        [enableMouse setState:NSControlStateValueOn];

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

        [getAppMain() setIgnoreMouse:false];

        // Activate this app to enable touchpad gesture.
        [NSApp activateIgnoringOtherApps:YES];
    } else {
        [getAppMain() setIgnoreMouse:true];
        if (NSApp.active && alterApp_) {
            [alterApp_ activateWithOptions:NSApplicationActivateIgnoringOtherApps];
        }
        alterApp_ = NULL;
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
    const auto origin = [getAppMain() getWindowPosition];
    return glm::vec2(pos.x - origin.x, pos.y - origin.y);
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
