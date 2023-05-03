#include <string_view>
#include <vector>
#include "toml.hpp"
#include "yommd.hpp"

Config::Config() :
    simulationFPS(60.0f), gravity(9.8f), defaultPosition(0.0f, 0.0f), defaultScale(1.0f)
{}

void Config::parse() {
    try {
        const std::string path = "./config.toml";
        const auto entire = toml::parse(path);

        model = toml::find<std::string>(entire, "model");

        const toml::array motionConfs = toml::find_or(entire, "motion", toml::array());
        for (const auto& motion : motionConfs) {
            bool enabled = toml::find_or(motion, "enabled", true);
            auto weight = toml::find_or<decltype(Motion::weight)>(motion, "weight", 1);
            std::string path = toml::find<std::string>(motion, "path");
            motions.push_back(Motion{
                .enabled = enabled,
                .weight = weight,
                .path = path,
            });
        }

        if (entire.contains("default-position")) {
            const auto pos = toml::find<std::array<float, 2>>(entire, "default-position");
            defaultPosition.x = pos[0];
            defaultPosition.y = pos[1];
        }
        defaultScale = toml::find_or(entire, "default-scale", defaultScale);
        simulationFPS = toml::find_or(entire, "simulation-fps", simulationFPS);
    } catch (std::runtime_error& e) {
        // File open error, file read error, etc...
        Err::Exit(e.what());
    } catch (std::exception& e) {
        Err::Log(e.what());
    } catch (std::out_of_range& e) {
        Err::Log(e.what());
    }
}
