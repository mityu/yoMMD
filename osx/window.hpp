// vim:set filetype=objcpp:
#ifndef WINDOW_HPP_
#define WINDOW_HPP_

#import <Cocoa/Cocoa.h>
#import <MetalKit/MetalKit.h>

@interface Window : NSWindow
@end

@interface WindowDelegate : NSObject <NSWindowDelegate>
@end

@interface View : MTKView
@end

@interface ViewDelegate : NSObject <MTKViewDelegate>
@end

#endif  // WINDOW_HPP_
