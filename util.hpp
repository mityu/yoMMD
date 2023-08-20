#ifndef UTIL_HPP_
#define UTIL_HPP_

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include "main.hpp"

struct NonCopyable {
    NonCopyable() = default;
    NonCopyable(const NonCopyable &) = delete;
    NonCopyable &operator=(const NonCopyable &) = delete;
};

struct CmdArgs {
    using Path = std::filesystem::path;
    Path cwd;
    Path configFile;
    Path logFile;

    static CmdArgs Parse(const std::vector<std::string>& args);
};

namespace Yommd {
void slogFunc(const char *tag, uint32_t logLevel, uint32_t logItem,
        const char *message, uint32_t linenr, const char *filename, void *user_data);
void makeAbsolute(std::filesystem::path& path, const std::filesystem::path& cwd);
}

namespace _internal {
template <typename T> void _log(std::ostream& os, T&& car) {
    os << car << std::endl;
}
template <typename T, typename... Args> void _log(std::ostream& os, T&& car, Args&&... cdr) {
    os << car << ' ';
    _log(os, std::forward<Args>(cdr)...);
}
}

namespace Info {
template <typename... Args> void Log(Args&&... args) {
    ::_internal::_log(std::cout, std::forward<Args>(args)...);
}
}
namespace Err {
template <typename... Args> void Log(Args&&... args) {
    std::stringstream ss;
    ::_internal::_log(ss, std::forward<Args>(args)...);
    Dialog::messageBox(ss.str());
}
template <typename... Args> [[noreturn]] void Exit(Args&&... args) {
    Log(std::forward<Args>(args)...);
    std::exit(1);
}
}

namespace String {
template <typename T>
inline std::u8string tou8(const std::basic_string<T>& str) {
    static_assert(sizeof(T) == sizeof(char8_t), "Invalid conversion.");
    return std::u8string(str.cbegin(), str.cend());
}
template <typename T>
inline std::u8string tou8(const std::basic_string_view<T> str) {
    static_assert(sizeof(T) == sizeof(char8_t), "Invalid conversion.");
    return std::u8string(str.cbegin(), str.cend());
}
#ifdef PLATFORM_WINDOWS
template <typename T>
inline std::basic_string<T> wideToMulti(const std::wstring_view wstr) {
    static_assert(sizeof(T) == sizeof(char), "Invalid conversion.");
    std::basic_string<T> str;
    int size = WideCharToMultiByte(
            CP_ACP, 0, wstr.data(), -1, nullptr, 0, nullptr, nullptr);
    str.resize(size-1, '\0');  // "size" includes padding for '\0'
    WideCharToMultiByte(CP_ACP, 0, wstr.data(), -1,
            reinterpret_cast<char *>(str.data()), size, nullptr, nullptr);
    return str;
}
#endif
}

namespace Enum {
template <typename T>
inline constexpr std::underlying_type_t<T> underlyCast(T v) {
    return static_cast<decltype(underlyCast(v))>(v);
}
}

#endif  // UTIL_HPP_
