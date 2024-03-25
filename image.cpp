#include "stb_image.h"
#include "image.hpp"
#include "platform.hpp"
#include <cstdio>
#include <string_view>

#ifdef PLATFORM_WINDOWS
#  include <windows.h>
#endif

class File : private NonCopyable {
public:
    File();
    File(const std::string_view path);
    ~File();
    void Open(const std::string_view path);
    void Close();
    operator FILE *();
    operator bool() const;
private:
    FILE *fp;
};

File::File() :
    fp(nullptr)
{}

File::File(const std::string_view path) {
    Open(path);
}

File::~File() {
    Close();
}

void File::Open(const std::string_view path) {
#ifdef PLATFORM_WINDOWS
    std::wstring wpath;
    const int size = MultiByteToWideChar(
            CP_UTF8, MB_COMPOSITE, path.data(), -1, nullptr, 0);
    wpath.resize(size-1, '\0');
    const int status = MultiByteToWideChar(
            CP_UTF8, MB_COMPOSITE, path.data(), -1, wpath.data(), size);
    if (!status)
        Err::Exit("String conversion failed: from:", path);
    fp = _wfopen(wpath.c_str(), L"rb");
#else
    fp = std::fopen(path.data(), "rb");
#endif
}

void File::Close() {
    if (fp) {
        std::fclose(fp);
        fp = nullptr;
    }
}

File::operator FILE *() {
    return fp;
}

File::operator bool() const {
    return fp != nullptr;
}

Image::Image() :
    width(0), height(0), dataSize(0), hasAlpha(false)
{}

Image::Image(Image&& image) {
    *this = std::move(image);
}

Image& Image::operator=(Image &&rhs) {
    width = rhs.width;
    height = rhs.height;
    dataSize = rhs.dataSize;
    pixels = rhs.pixels;
    hasAlpha = rhs.hasAlpha;

    return *this;
}

bool Image::loadFromFile(const std::string_view path) {
    // TODO: Is this really needed?
    stbi_set_flip_vertically_on_load(true);
    File file(path);

    if (!file) {
        Err::Log("Failed to open file:", path);
        return false;
    }

    int comp = 0;
    const int ret = stbi_info_from_file(file, &width, &height, &comp);
    if (ret == 0) {
        Err::Log("Failed to read info:", path, ':', stbi_failure_reason());
        return false;
    }

    if (comp == 4)
        hasAlpha = true;
    else
        hasAlpha = false;

    uint8_t * const image = stbi_load_from_file(file, &width, &height, &comp, STBI_rgb_alpha);
    dataSize = width * height * 4;
    pixels.resize(dataSize);
    std::copy(image, image + dataSize, pixels.data());
    stbi_image_free(image);

    return true;
}

bool Image::loadFromMemory(const Resource::View& resource) {
    stbi_set_flip_vertically_on_load(true);

    int comp = 0;
    const int ret =
        stbi_info_from_memory(resource.data(), resource.length(), &width, &height, &comp);
    if (ret == 0) {
        Err::Log("Failed to read info:", __func__, ':', stbi_failure_reason());
        return false;
    }

    if (comp == 4)
        hasAlpha = true;
    else
        hasAlpha = false;

    uint8_t * const image =
        stbi_load_from_memory(resource.data(), resource.length(), &width, &height, &comp, STBI_rgb_alpha);
    dataSize = width * height * 4;
    pixels.resize(dataSize);
    std::copy(image, image + dataSize, pixels.data());
    stbi_image_free(image);

    return true;
}
