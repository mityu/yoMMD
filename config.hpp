#ifndef CONFIG_HPP_
#define CONFIG_HPP_

#include <filesystem>
#include <optional>
#include <vector>
#include "glm/glm.hpp"

struct Config {
    using Path = std::filesystem::path;

    struct Motion {
        bool disabled;
        unsigned int weight;
        std::vector<Path> paths;
    };
    Config();

    Path model;
    std::vector<Motion> motions;
    float simulationFPS;
    float gravity;
    glm::vec3 lightDirection;
    glm::vec2 defaultModelPosition;
    float defaultScale;
    glm::vec3 defaultCameraPosition;
    glm::vec3 defaultGazePosition;
    std::optional<int> defaultScreenNumber;

    static Config Parse(const std::filesystem::path& configFile);
};

#endif  // CONFIG_HPP_
