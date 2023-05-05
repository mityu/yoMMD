#include <string_view>
#include <filesystem>
#include <vector>
#include "toml.hpp"
#include "yommd.hpp"

Config::Config() :
    simulationFPS(60.0f), gravity(9.8f), defaultPosition(0.0f, 0.0f), defaultScale(1.0f)
{}

Config Config::Parse(const std::filesystem::path& configFile) {
    namespace fs = std::filesystem;

    Config config;
    auto configDir = fs::path(configFile).parent_path();

    try {
        const auto entire = toml::parse(configFile);

        config.model = fs::u8path(toml::find<std::string>(entire, "model"));
        if (config.model.is_relative())
            config.model = configDir / config.model;

        const toml::array motions = toml::find_or(entire, "motion", toml::array());
        for (const auto& motion : motions) {
            bool enabled = toml::find_or(motion, "enabled", true);
            auto weight = toml::find_or<decltype(Motion::weight)>(motion, "weight", 1);
            auto path = fs::u8path(toml::find<std::string>(motion, "path"));
            if (path.is_relative())
                path = configDir / path;
            config.motions.push_back(Motion{
                .enabled = enabled,
                .weight = weight,
                .path = path,
            });
        }

        if (entire.contains("default-position")) {
            const auto pos = toml::find<std::array<float, 2>>(entire, "default-position");
            config.defaultPosition.x = pos[0];
            config.defaultPosition.y = pos[1];
        }
        config.defaultScale = toml::find_or(entire, "default-scale", config.defaultScale);
        config.simulationFPS = toml::find_or(entire, "simulation-fps", config.simulationFPS);
    } catch (std::runtime_error& e) {
        // File open error, file read error, etc...
        Err::Exit(e.what());
    } catch (std::out_of_range& e) {
        Err::Log(e.what());
    } catch (std::exception& e) {
        Err::Log(e.what());
    }

    return config;
}
