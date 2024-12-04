#include "menu.hpp"
#include "../util.hpp"
#include "main.hpp"

@interface SelectScreenMenuDelegate : NSObject <NSMenuDelegate>
@end

namespace {
// Get currently active application.  If it's not found, returns NULL.
NSRunningApplication *getActiveApplication(void);
}  // namespace

@implementation SelectScreenMenuDelegate {
    NSWindow *window_;
}
- (instancetype)init {
    self = [super init];
    if (self) {
        constexpr NSUInteger style = NSWindowStyleMaskBorderless;
        constexpr NSUInteger collectionBehavior =
            NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorStationary |
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
- (void)menuDidClose:(NSMenu *)menu {
    [window_ setIsVisible:NO];
    [window_ setFrame:NSMakeRect(0, 0, 0, 0) display:NO];
}
- (void)menu:(NSMenu *)menu willHighlightItem:(NSMenuItem *)item {
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
- (instancetype)init {
    self = [super init];
    if (self) {
        alterApp_ = nil;
        isMenuOpened_ = false;

        // Menu title and action is is set later in menuNeedsUpdate.
        NSMenuItem *enableMouse = [[NSMenuItem alloc] initWithTitle:@""
                                                             action:nil
                                                      keyEquivalent:@""];
        [enableMouse setTag:Enum::underlyCast(MenuTag::EnableMouse)];
        [enableMouse setTarget:self];

        NSMenuItem *resetModelPosition =
            [[NSMenuItem alloc] initWithTitle:@"Reset Position"
                                       action:@selector(actionResetModelPosition:)
                                keyEquivalent:@""];
        [resetModelPosition setTag:Enum::underlyCast(MenuTag::None)];
        [resetModelPosition setTarget:self];

        selectScreenMenuDelegate_ = [[SelectScreenMenuDelegate alloc] init];
        NSMenuItem *selectScreen = [[NSMenuItem alloc] initWithTitle:@"Select screen"
                                                              action:nil
                                                       keyEquivalent:@""];
        [selectScreen setTag:Enum::underlyCast(MenuTag::SelectScreen)];
        [selectScreen setSubmenu:[[NSMenu alloc] init]];
        [selectScreen.submenu setDelegate:selectScreenMenuDelegate_];
        // In order to be able to disable menu items manually.
        [selectScreen.submenu setAutoenablesItems:NO];
        [selectScreen setTarget:self];

        // Menu title and action is is set later in menuNeedsUpdate.
        NSMenuItem *hideWindow = [[NSMenuItem alloc] initWithTitle:@""
                                                            action:nil
                                                     keyEquivalent:@""];
        [hideWindow setTag:Enum::underlyCast(MenuTag::HideWindow)];
        [hideWindow setTarget:self];

        NSMenuItem *quit = [[NSMenuItem alloc] initWithTitle:@"Quit"
                                                      action:@selector(actionQuit:)
                                               keyEquivalent:@""];
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
- (void)menu:(NSMenu *)menu willHighlightItem:(NSMenuItem *)item {
    if (!item || item.tag != Enum::underlyCast(MenuTag::SelectScreen))
        return;

    NSNumber *currentScreenID = [getAppMain() getCurrentScreenNumber];

    NSMenu *subMenu = item.submenu;
    [subMenu removeAllItems];
    for (NSScreen *screen in [NSScreen screens]) {
        NSNumber *scID = [screen deviceDescription][@"NSScreenNumber"];
        NSString *title = [[NSString alloc] initWithFormat:@"Screen%@", [scID stringValue]];
        NSMenuItem *subItem = [[NSMenuItem alloc] initWithTitle:title
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
- (void)menuNeedsUpdate:(NSMenu *)menu {
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
- (void)menuWillOpen:(NSMenu *)menu {
    isMenuOpened_ = true;
}
- (void)menuDidClose:(NSMenu *)menu {
    isMenuOpened_ = false;
}
- (bool)isMenuOpened {
    return isMenuOpened_;
}
- (void)actionQuit:(id)sender {
    [NSApp terminate:sender];
}
- (void)actionEnableMouse:(NSMenuItem *)sender {
    alterApp_ = getActiveApplication();

    [getAppMain() setIgnoreMouse:false];

    // Activate this app to enable touchpad gesture.
    [NSApp activateIgnoringOtherApps:YES];
}
- (void)actionDisableMouse:(NSMenuItem *)sender {
    [getAppMain() setIgnoreMouse:true];
    if (NSApp.active && alterApp_) {
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 140000
        [alterApp_ activateWithOptions:0];
#else
        [alterApp_ activateWithOptions:NSApplicationActivateIgnoringOtherApps];
#endif
    }
    alterApp_ = NULL;
}
- (void)actionHideWindow:(NSMenuItem *)sender {
    [NSApp hide:self];
}
- (void)actionShowWindow:(NSMenuItem *)sender {
    NSRunningApplication *activeApp = getActiveApplication();
    [NSApp unhide:self];
    if ([getAppMain() getIgnoreMouse]) {
        if (NSApp.active && activeApp) {
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 140000
            [activeApp activateWithOptions:0];
#else
            [activeApp activateWithOptions:NSApplicationActivateIgnoringOtherApps];
#endif
        }
    } else if (!NSApp.active) {
        [NSApp activateIgnoringOtherApps:YES];
    }
}
- (void)actionResetModelPosition:(NSMenuItem *)sender {
    [getAppMain() getRoutine].ResetModelPosition();
}
- (void)actionChangeScreen:(NSMenuItem *)sender {
    [getAppMain() changeWindowScreen:sender.tag];
}
@end

namespace {
NSRunningApplication *getActiveApplication(void) {
    NSArray *appList = [[NSWorkspace sharedWorkspace] runningApplications];
    for (NSRunningApplication *app in appList) {
        if (app.active) {
            return app;
        }
    }
    return NULL;
}
}  // namespace
