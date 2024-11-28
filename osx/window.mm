#include "window.hpp"
#include <array>
#include <functional>
#include <type_traits>
#include <utility>
#include "../keyboard.hpp"
#include "main.hpp"

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

GestureController::GestureController() : shouldSkip_(false) {}

void GestureController::SkipThisGesture() {
    shouldSkip_ = true;
}

template <typename T>
void GestureController::Emit(
    NSEvent *event,
    std::function<void()> worker,
    const T& cancelKeys) {
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
        for (const auto key : cancelKeys) {
            prevKeyState_[static_cast<std::size_t>(key)] = Keyboard::IsKeyPressed(key);
        }
    }
    for (const auto key : cancelKeys) {
        if (prevKeyState_[static_cast<std::size_t>(key)] != Keyboard::IsKeyPressed(key)) {
            SkipThisGesture();
            return;
        }
    }
    worker();
}

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
    constexpr std::array<Keycode, 1> cancelKeys = {Keycode::Shift};
    float delta = event.deltaY * 10.0f;  // TODO: Better factor
    if (event.hasPreciseScrollingDeltas)
        delta = event.scrollingDeltaY;

    if (!event.directionInvertedFromDevice)
        delta = -delta;

    const auto worker = [delta]() { [getAppMain() getRoutine].OnWheelScrolled(delta); };
    gestureController_.Emit(event, worker, cancelKeys);
}
- (void)magnifyWithEvent:(NSEvent *)event {
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
    const auto worker = [&phase, &event]() {
        [getAppMain() getRoutine].OnGestureZoom(phase, event.magnification);
    };
    gestureController_.Emit(event, worker, GestureController::WontCancel);
}
- (void)smartMagnifyWithEvent:(NSEvent *)event {
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
- (void)windowDidChangeScreen:(NSNotification *)notification {
    NSWindow *window = [notification object];
    const NSScreen *screen = [window screen];
    if (!NSEqualRects(window.frame, screen.visibleFrame)) {
        [window setFrame:screen.visibleFrame display:YES animate:NO];
    }
}
- (void)windowWillClose:(NSNotification *)notification {
    [NSApp terminate:self];
}
@end

@implementation View
+ (NSMenu *)defaultMenu {
    return [getAppMain() getAppMenu];
}
- (BOOL)acceptsFirstMouse:(NSEvent *)event {
    return NO;
}
@end

@implementation ViewDelegate
- (void)mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size {
}
- (void)drawInMTKView:(nonnull MTKView *)view {
    @autoreleasepool {
        auto& routine = [getAppMain() getRoutine];
        routine.Update();
        routine.Draw();
    }
}
@end
