#ifndef MAIN_HPP_
#define MAIN_HPP_

#include "glm/vec2.hpp" // IWYU pragma: keep; silence clangd.
#include "sokol_gfx.h"
#include <string_view>

namespace Dialog {
void messageBox(std::string_view msg);
}

namespace Context {
sg_environment getSokolEnvironment();
sg_swapchain getSokolSwapchain();
glm::vec2 getWindowSize();
glm::vec2 getDrawableSize();
int getSampleCount();

// Get mouse position of local to window.  The origin should be left-bottom of
// window.
glm::vec2 getMousePosition();
bool shouldEmphasizeModel();
}

#endif  // MAIN_HPP_
