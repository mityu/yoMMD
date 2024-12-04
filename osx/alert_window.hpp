// vim:set filetype=objcpp:
#ifndef ALERT_WINDOW_HPP_
#define ALERT_WINDOW_HPP_

#import <Cocoa/Cocoa.h>
#import <Foundation/Foundation.h>

@interface AlertWindow : NSObject
- (void)showAlert:(NSString *)msg;
- (void)actionClose:(id)sender;
@end

#endif  // ALERT_WINDOW_HPP_
