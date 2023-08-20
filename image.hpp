#ifndef IMAGE_HPP_
#define IMAGE_HPP_

#include "util.hpp"
#include "resources.hpp"

class Image : private NonCopyable {
public:
    std::vector<uint8_t> pixels;
    int width;
    int height;
    size_t dataSize;
    bool hasAlpha;

    Image();
    Image(Image&& image);
    Image &operator=(Image &&rhs);
    bool loadFromFile(const std::string_view path);
    bool loadFromMemory(const Resource::View& resource);
private:
};

#endif  // IMAGE_HPP_
