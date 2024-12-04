#ifndef MENU_HPP_
#define MENU_HPP_

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

@interface AppMenuDelegate : NSObject <NSMenuDelegate>
- (bool)isMenuOpened;
- (void)actionQuit:(NSMenuItem *)sender;
- (void)actionEnableMouse:(NSMenuItem *)sender;
- (void)actionDisableMouse:(NSMenuItem *)sender;
- (void)actionHideWindow:(NSMenuItem *)sender;
- (void)actionShowWindow:(NSMenuItem *)sender;
- (void)actionResetModelPosition:(NSMenuItem *)sender;
- (void)actionChangeScreen:(NSMenuItem *)sender;
@end

#endif  // MENU_HPP_
