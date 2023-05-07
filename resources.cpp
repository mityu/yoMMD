// Handles embedded data.
#include <string_view>
#include <filesystem>
#include "yommd.hpp"

#define INCBIN_PREFIX _
#include "incbin.h"

extern "C" {
INCBIN(Toon01, "toons/toon01.bmp");
INCBIN(Toon02, "toons/toon02.bmp");
INCBIN(Toon03, "toons/toon03.bmp");
INCBIN(Toon04, "toons/toon04.bmp");
INCBIN(Toon05, "toons/toon05.bmp");
INCBIN(Toon06, "toons/toon06.bmp");
INCBIN(Toon07, "toons/toon07.bmp");
INCBIN(Toon08, "toons/toon08.bmp");
INCBIN(Toon09, "toons/toon09.bmp");
INCBIN(Toon10, "toons/toon10.bmp");
}

namespace Resource {
namespace fs = std::filesystem;

const unsigned char *View::data() const {
    return first;
}

std::size_t View::length() const {
    return second;
}

View getToonData(std::string_view path) {
    const std::string basename = fs::path(Yommd::toUtf8String(path)).filename().string();
    if (basename == "toon01.bmp") {
        return {_Toon01Data, _Toon01Size};
    } else if (basename == "toon02.bmp") {
        return {_Toon02Data, _Toon02Size};
    } else if (basename == "toon03.bmp") {
        return {_Toon03Data, _Toon03Size};
    } else if (basename == "toon04.bmp") {
        return {_Toon04Data, _Toon04Size};
    } else if (basename == "toon05.bmp") {
        return {_Toon05Data, _Toon05Size};
    } else if (basename == "toon06.bmp") {
        return {_Toon06Data, _Toon06Size};
    } else if (basename == "toon07.bmp") {
        return {_Toon07Data, _Toon07Size};
    } else if (basename == "toon08.bmp") {
        return {_Toon08Data, _Toon08Size};
    } else if (basename == "toon09.bmp") {
        return {_Toon09Data, _Toon09Size};
    } else if (basename == "toon10.bmp") {
        return {_Toon10Data, _Toon10Size};
    } else {
        Err::Log("Internal error:");
        return {nullptr, 0};
    }
}
}
