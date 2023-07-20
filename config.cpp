#include <string_view>
#include <filesystem>
#include <vector>
#include "toml.hpp"
#include "yommd.hpp"

Config::Config() :
    simulationFPS(60.0f), gravity(9.8f),
    defaultModelPosition(0.0f, 0.0f), defaultScale(1.0f),
    defaultCameraPosition(0, 10, 50), defaultGazePosition(0, 10, 0)
{}

Config Config::Parse(const std::filesystem::path& configFile) {
    namespace fs = std::filesystem;

    Config config;
    auto configDir = fs::path(configFile).parent_path();

    try {
        const auto entire = toml::parse(configFile);

        config.model = fs::path(String::tou8(toml::find<std::string>(entire, "model")));
        Yommd::makeAbsolute(config.model, configDir);

        const toml::array motions = toml::find_or(
                entire, "motion", toml::array());
        for (const auto& motion : motions) {
            bool disabled = toml::find_or(motion, "disabled", false);
            auto weight = toml::find_or<decltype(
                    Motion::weight)>(motion, "weight", 1);
            auto raw_path = toml::find<std::vector<std::string>>(motion, "path");
            std::vector<fs::path> path;
            for (const auto& p : raw_path) {
                auto u8path = fs::path(String::tou8(p));
                Yommd::makeAbsolute(u8path, configDir);
                path.push_back(u8path);
            }
            config.motions.push_back(Motion{
                .disabled = disabled,
                .weight = weight,
                .paths = std::move(path),
            });
        }

        if (entire.contains("default-model-position")) {
            const auto pos = toml::find<std::array<float, 2>>(
                    entire, "default-model-position");
            config.defaultModelPosition.x = pos[0];
            config.defaultModelPosition.y = pos[1];
        }

        if (entire.contains("default-camera-position")) {
            const auto pos = toml::find<std::array<float, 3>>(
                    entire, "default-camera-position");
            config.defaultCameraPosition.x = pos[0];
            config.defaultCameraPosition.y = pos[1];
            config.defaultCameraPosition.z = pos[2];
        }

        if (entire.contains("default-gaze-position")) {
            const auto pos = toml::find<std::array<float, 3>>(
                    entire, "default-gaze-position");
            config.defaultGazePosition.x = pos[0];
            config.defaultGazePosition.y = pos[1];
            config.defaultGazePosition.z = pos[2];
        }

        config.defaultScale = toml::find_or(
                entire, "default-scale", config.defaultScale);
        config.simulationFPS = toml::find_or(
                entire, "simulation-fps", config.simulationFPS);
        config.gravity = toml::find_or(entire, "gravity", config.gravity);
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
