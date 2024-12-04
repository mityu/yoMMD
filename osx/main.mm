#include "main.hpp"
#include "../constant.hpp"
#include "../keyboard.hpp"
#include "../platform_api.hpp"
#include "../util.hpp"
#include "../viewer.hpp"
#include "alert_window.hpp"
#include "menu.hpp"
#include "window.hpp"

@interface AppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation AppDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    NSArray *argsArray = [[NSProcessInfo processInfo] arguments];
    std::vector<std::string> argsVec;
    for (const NSString *arg in argsArray) {
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

@implementation AppMain {
    Window *window_;
    WindowDelegate *windowDelegate_;
    View *view_;
    id<MTLDevice> metalDevice_;
    ViewDelegate *viewDelegate_;
    NSStatusItem *statusItem_;
    NSMenu *appMenu_;
    AppMenuDelegate *appMenuDelegate_;
    Routine routine_;
}
- (void)createMainWindow {
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
        NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorStationary |
        NSWindowCollectionBehaviorTransient | NSWindowCollectionBehaviorFullScreenAuxiliary |
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
    [view_ setDevice:metalDevice_];
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
- (void)createStatusItem {
    const auto iconData = Resource::getStatusIconData();
    NSData *nsIconData = [NSData dataWithBytes:iconData.data() length:iconData.length()];
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
- (void)setIgnoreMouse:(bool)enable {
    [window_ setIgnoresMouseEvents:enable];
    if (!enable)
        Keyboard::ResetAllState();
}
- (bool)getIgnoreMouse {
    return [window_ ignoresMouseEvents];
}
- (void)changeWindowScreen:(NSUInteger)scID {
    const NSScreen *dst = findScreenFromID(scID);
    if (!dst) {
        // Display not found.  Maybe the connection for the target display is
        // removed.
        Info::Log("Display not found: %ld", scID);
        return;
    }
    [window_ setFrame:dst.visibleFrame display:YES animate:NO];
}
- (NSMenu *)getAppMenu {
    // NOTE: Appropriating "appMenu_" here will break status menu after using
    // right click menu.  (Status menu will disappear after a use of right
    // click menu.)  As a workaround, make a new NSMenu object and use it.
    NSMenu *menu = [[NSMenu alloc] init];
    [menu setDelegate:appMenuDelegate_];
    return menu;
}
- (sg_environment)getSokolEnvironment {
    return sg_environment{
        .defaults =
            {
                .color_format = SG_PIXELFORMAT_BGRA8,
                .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
                .sample_count = Constant::PreferredSampleCount,
            },
        .metal =
            {
                .device = (__bridge const void *)metalDevice_,
            },
    };
}
- (sg_swapchain)getSokolSwapchain {
    const auto size{Context::getWindowSize()};
    return sg_swapchain{
        .width = static_cast<int>(size.x),
        .height = static_cast<int>(size.y),
        .sample_count = Constant::PreferredSampleCount,
        .color_format = SG_PIXELFORMAT_BGRA8,
        .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
        .metal =
            {
                .current_drawable = (__bridge const void *)[view_ currentDrawable],
                .depth_stencil_texture = (__bridge const void *)[view_ depthStencilTexture],
                .msaa_color_texture = (__bridge const void *)[view_ multisampleColorTexture],
            },
    };
}
- (NSSize)getWindowSize {
    return window_.frame.size;
}
- (NSPoint)getWindowPosition {
    return window_.frame.origin;
}
- (NSSize)getDrawableSize {
    return view_.drawableSize;
}
- (NSNumber *)getCurrentScreenNumber {
    const NSScreen *screen = [window_ screen];
    if (!screen)
        Err::Log("Internal error? screen is offscreen");
    return [screen deviceDescription][@"NSScreenNumber"];
}
- (bool)isMenuOpened {
    return [appMenuDelegate_ isMenuOpened];
}
- (Routine&)getRoutine {
    return routine_;
}
- (void)startDrawingModel {
    [view_ setDelegate:viewDelegate_];
}
@end

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
}  // namespace Dialog

int main() {
    [NSApplication sharedApplication];

    auto appDelegate = [[AppDelegate alloc] init];

    [NSApp setDelegate:appDelegate];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [NSApp activateIgnoringOtherApps:NO];
    [NSApp run];
}
