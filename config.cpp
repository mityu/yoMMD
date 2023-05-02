#include <string_view>
#include "toml.hpp"
#include "yommd.hpp"

namespace {
template <typename T>
T find_or(const toml::value& data, std::string_view key, T defValue) {
    const toml::value val = toml::find_or(data, key, defValue);
}
}

Config::Config() :
    simulationFPS_(60.0f), gravity_(9.8f)
{}

void Config::parse() {
    try {
        const std::string path = "./config.toml";
        const auto entire = toml::parse(path);

        model_ = toml::find<std::string>(entire, "model");

        const toml::array motions = toml::find_or(entire, "motion", toml::array());
        for (const auto& motion : motions) {
            bool enabled = toml::find_or(motion, "enabled", true);
            auto weight = toml::find_or<decltype(Motion::weight)>(motion, "weight", 1);
            std::string path = toml::find<std::string>(motion, "path");
            motions_.push_back(Motion{
                        .enabled = enabled,
                        .weight = weight,
                        .path = path,
                    });
        }

        simulationFPS_ = toml::find_or(entire, "SimulationFPS", simulationFPS_);
    } catch (std::runtime_error e) {
        // File open error, file read error, etc...
        Err::Exit(e.what());
    } catch (std::exception e) {
        Err::Log(e.what());
    } catch (std::out_of_range e) {
        Err::Log(e.what());
    }
}

const std::string& Config::getModel() const {
    return model_;
}

const std::vector<Config::Motion>& Config::getMotions() const {
    return motions_;
}

float Config::getSimulationFPS() const {
    return simulationFPS_;
}
