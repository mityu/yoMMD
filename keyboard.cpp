#include "keyboard.hpp"
#include <array>
#include <cstddef>

namespace {
std::array<bool, static_cast<std::size_t>(Keycode::Count)> state({});
}

bool Keyboard::IsKeyPressed(Keycode code) {
    return state[static_cast<std::size_t>(code)];
}

void Keyboard::OnKeyDown(Keycode code) {
    state[static_cast<std::size_t>(code)] = true;
}

void Keyboard::OnKeyUp(Keycode code) {
    state[static_cast<std::size_t>(code)] = false;
}

void Keyboard::ResetAllState() {
    state.fill(false);
}
