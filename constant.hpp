#ifndef CONSTANT_HPP_
#define CONSTANT_HPP_

#include <string_view>

namespace Constant {
constexpr int PreferredSampleCount = 4;
constexpr float FPS = 60.0f;
constexpr float VmdFPS = 30.0f;
constexpr std::string_view DefaultLogFilePath = "";
}

#endif  // CONSTANT_HPP_
