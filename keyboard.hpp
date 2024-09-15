#ifndef KEYBOARD_HPP_
#define KEYBOARD_HPP_

enum class Keycode { Shift, Count };

namespace Keyboard {
bool IsKeyPressed(Keycode code);
void OnKeyDown(Keycode code);
void OnKeyUp(Keycode code);
void ResetAllState();
}  // namespace Keyboard

#endif  // KEYBOARD_HPP_
