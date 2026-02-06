#include <glm/vec2.hpp>
#include <iostream>
#include <string>
#include <vector>
#include "../constant.hpp"
#include "../platform_api.hpp"
#include "../util.hpp"
#include "../viewer.hpp"
#include "sokol_app.h"
#include "sokol_glue.h"

namespace {
// class ContextStore {
// private:
//     sg_environment environment_;
//     sg_swapchain swapchain_;
// };
constexpr int width = 800;
constexpr int height = 600;

Routine& getRoutine() {
    static Routine routine;
    return routine;
}

void init_cb() {
    getRoutine().Init();
}

void frame_cb() {
    auto& routine = getRoutine();
    routine.Update();
    routine.Draw();
}

void cleanup_cb() {
    getRoutine().Terminate();
}

}  // namespace

namespace Dialog {
void messageBox(std::string_view msg) {
    // TODO: Implement
    std::cout << msg << std::endl;
}
}  // namespace Dialog

namespace Context {
sg_environment getSokolEnvironment() {
    return sglue_environment();
}

sg_swapchain getSokolSwapchain() {
    return sglue_swapchain();
}

glm::vec2 getWindowSize() {
    return glm::vec2(sapp_width(), sapp_height());
}

glm::vec2 getDrawableSize() {
    // const auto swapchain = sglue_swapchain();
    // return glm::vec2(swapchain.width, swapchain.height);
    return glm::vec2(width, height);
}

int getSampleCount() {
    return sapp_sample_count();
}

glm::vec2 getMousePosition() {
    return glm::vec2(0, 0);  // TODO
}

bool shouldEmphasizeModel() {
    return false;
}

}  // namespace Context

sapp_desc sokol_main(int argc, char **argv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        args.push_back(argv[i]);
    }
    const auto cmdArgs = CmdArgs::Parse(args);
    args.clear();
    getRoutine().ParseConfig(cmdArgs);

    return sapp_desc{
        .init_cb = init_cb,
        .frame_cb = frame_cb,
        .cleanup_cb = cleanup_cb,
        .width = width,
        .height = height,
        .sample_count = Constant::PreferredSampleCount,
        .alpha = true,
        .window_title = "yoMMD",
    };
}
