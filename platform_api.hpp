#ifndef PLATFORM_API_HPP_
#define PLATFORM_API_HPP_

#include <string_view>
#include "glm/vec2.hpp"  // IWYU pragma: keep; silence clangd.
#include "sokol_gfx.h"

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
}  // namespace Context

#endif  // PLATFORM_API_HPP_
