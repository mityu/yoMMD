// vim:set filetype=objcpp:
#ifndef MAIN_HPP_
#define MAIN_HPP_

#import <Cocoa/Cocoa.h>
#import <MetalKit/MetalKit.h>

#include "../viewer.hpp"
#include "sokol_gfx.h"

@interface AppMain : NSObject
- (void)createMainWindow;
- (void)createStatusItem;
- (void)setIgnoreMouse:(bool)enable;
- (bool)getIgnoreMouse;  // Returns TRUE if this application ignores mouse events.
- (void)changeWindowScreen:(NSUInteger)scID;
- (NSMenu *)getAppMenu;
- (sg_environment)getSokolEnvironment;
- (sg_swapchain)getSokolSwapchain;
- (NSSize)getWindowSize;
- (NSPoint)getWindowPosition;
- (NSSize)getDrawableSize;
- (NSNumber *)getCurrentScreenNumber;
- (bool)isMenuOpened;
- (Routine&)getRoutine;
- (void)startDrawingModel;
@end

AppMain *getAppMain(void);

// Find NSScreen from NSScreenNumber.  If screen is not found, returns nil.
NSScreen *findScreenFromID(NSInteger scID);

#endif  // MAIN_HPP_
