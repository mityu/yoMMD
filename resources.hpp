#ifndef RESOURCES_HPP_
#define RESOURCES_HPP_

#include <string_view>
#include <utility>

namespace Resource {
class View : private std::pair<const unsigned char *, std::size_t> {
public:
    using pair::pair;
    const unsigned char *data() const;
    std::size_t length() const;

private:
};
View getToonData(std::string_view path);
View getStatusIconData();
}  // namespace Resource

#endif  // RESOURCES_HPP_
