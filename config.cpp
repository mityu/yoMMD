#include "config.hpp"
#include <filesystem>
#include <string_view>
#include <vector>
#include "toml.hpp"  // IWYU pragma: keep; supress warning from clangd.
#include "util.hpp"

namespace {
inline glm::vec2 toVec2(const std::array<float, 2> a) {
    return glm::vec2(a[0], a[1]);
}

inline glm::vec3 toVec3(const std::array<float, 3> a) {
    return glm::vec3(a[0], a[1], a[2]);
}

}  // namespace

Config::Config() :
    simulationFPS(60.0f),
    gravity(9.8f),
    lightDirection(-0.5f, -1.0f, -0.5f),
    defaultModelPosition(0.0f, 0.0f),
    defaultScale(1.0f),
    defaultCameraPosition(0, 10, 50),
    defaultGazePosition(0, 10, 0),
    defaultScreenNumber(std::nullopt) {}

Config Config::Parse(const std::filesystem::path& configFile) {
    namespace fs = std::filesystem;

    constexpr auto warnUnsupportedKey = [](const toml::value::key_type& k,
                                           const toml::value& v) {
        // TODO: Error message should point key, not its value
        constexpr std::string_view header = "[error]";
        const std::string rawmsg = toml::format_error(
            "Ignoring unsupported config key.", v, "Key is not supported: " + k);
        std::string_view errmsg = rawmsg;
        if (errmsg.starts_with(header)) {
            errmsg.remove_prefix(header.size());
            while (errmsg.front() == ' ')
                errmsg.remove_prefix(1);
        }
        Err::Log("[warning]", errmsg);
    };

    Config config;
    const auto configDir = fs::path(configFile).parent_path();

    try {
        const auto entire = toml::parse(configFile);

        // Ensure all the required keys appear in config.toml
        (void)toml::find(entire, "model");

        for (const auto& [k, v] : entire.as_table()) {
            if (k == "model") {
                const auto path = toml::get<std::u8string>(v);
                config.model = ::Path::makeAbsolute(fs::path(path), configDir);
            } else if (k == "default-model-position") {
                const auto pos = toml::get<std::array<float, 2>>(v);
                config.defaultModelPosition = toVec2(pos);
            } else if (k == "default-camera-position") {
                const auto pos = toml::get<std::array<float, 3>>(v);
                config.defaultCameraPosition = toVec3(pos);
            } else if (k == "default-gaze-position") {
                const auto pos = toml::get<std::array<float, 3>>(v);
                config.defaultGazePosition = toVec3(pos);
            } else if (k == "default-scale") {
                config.defaultScale = v.as_floating();
            } else if (k == "simulation-fps") {
                config.simulationFPS = v.as_floating();
            } else if (k == "gravity") {
                config.gravity = v.as_floating();
            } else if (k == "light-direction") {
                const auto d = toml::get<std::array<float, 3>>(v);
                config.lightDirection = toVec3(d);
            } else if (k == "default-screen-number") {
                config.defaultScreenNumber = v.as_integer();
            } else if (k == "motion") {
                for (const auto& m : v.as_array()) {
                    // Ensure all the required key appear in "motion" table.
                    (void)toml::find(m, "path");

                    Motion c = {.disabled = false, .weight = 1};
                    for (const auto& [k, v] : m.as_table()) {
                        if (k == "path") {
                            const auto raw_path = toml::get<std::vector<std::u8string>>(v);
                            std::vector<fs::path> path;
                            for (const auto& p : raw_path) {
                                path.push_back(::Path::makeAbsolute(fs::path(p), configDir));
                            }
                            c.paths = std::move(path);
                        } else if (k == "weight") {
                            c.weight = v.as_integer();
                            if (c.weight <= 0) {
                                const auto errmsg = toml::format_error(
                                    "Invalid value for \"weight\"", v,
                                    "Value must be bigger than or equals to 1.");
                                Err::Log(errmsg);
                            }
                        } else if (k == "disabled") {
                            c.disabled = v.as_boolean();
                        } else {
                            warnUnsupportedKey(k, v);
                        }
                    }
                    config.motions.push_back(std::move(c));
                }
            } else {
                warnUnsupportedKey(k, v);
            }
        }
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
