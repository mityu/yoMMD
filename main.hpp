#ifndef MAIN_HPP_
#define MAIN_HPP_

#include <string_view>
#include "glm/glm.hpp"
#include "sokol_gfx.h"

namespace Dialog {
void messageBox(std::string_view msg);
}

namespace Context {
sg_context_desc getSokolContext();
glm::vec2 getWindowSize();
glm::vec2 getDrawableSize();

// Get mouse position of local to window.  The origin should be left-bottom of
// window.
glm::vec2 getMousePosition();
}

#endif  // MAIN_HPP_
