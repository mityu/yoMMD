#include <cstdio>
#include <string_view>
#include "stb_image.h"
#include "yommd.hpp"

class File : private NonCopyable {
public:
    File();
    File(const std::string_view path);
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

void File::Open(const std::string_view path) {
    fp = std::fopen(path.data(), "r");
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

    if (!file)
        return false;

    int comp = 0;
    int ret = stbi_info_from_file(file, &width, &height, &comp);
    if (ret == 0)
        return false;

    if (comp == 4)
        hasAlpha = true;
    else
        hasAlpha = false;

    uint8_t *image = stbi_load_from_file(file, &width, &height, &comp, STBI_rgb_alpha);
    dataSize = width * height * 4;
    pixels.resize(dataSize);
    std::copy(image, image + dataSize, pixels.data());
    stbi_image_free(image);

    return true;
}
