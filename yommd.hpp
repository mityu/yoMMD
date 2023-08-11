#ifndef YOMMD_HPP_
#define YOMMD_HPP_

#include <type_traits>
#include <optional>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include <iostream>
#include <cstdlib>
#include <sstream>
#include <string_view>
#include <map>
#include <filesystem>
#include <utility>
#include "Saba/Model/MMD/MMDMaterial.h"
#include "Saba/Model/MMD/MMDModel.h"
#include "Saba/Model/MMD/VMDAnimation.h"
#include "Saba/Model/MMD/VMDCameraAnimation.h"
#include "glm/glm.hpp"
#include "sokol_gfx.h"

#include "platform.hpp"

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace Constant {
constexpr int SampleCount = 4;
constexpr float FPS = 60.0f;
constexpr float VmdFPS = 30.0f;
constexpr std::string_view DefaultLogFilePath = "";
}

struct NonCopyable {
    NonCopyable() = default;
    NonCopyable(const NonCopyable &) = delete;
    NonCopyable &operator=(const NonCopyable &) = delete;
};

namespace _internal {
template <typename T> void _log(std::ostream& os, T&& car) {
    os << car << std::endl;
}
template <typename T, typename... Args> void _log(std::ostream& os, T&& car, Args&&... cdr) {
    os << car << ' ';
    _log(os, std::forward<Args>(cdr)...);
}
}

namespace Info {
template <typename... Args> void Log(Args&&... args) {
    ::_internal::_log(std::cout, std::forward<Args>(args)...);
}
}

// main_osx.mm/main_window.cpp
namespace Dialog {
void messageBox(std::string_view msg);
}

namespace Err {
template <typename... Args> void Log(Args&&... args) {
    std::stringstream ss;
    ::_internal::_log(ss, std::forward<Args>(args)...);
    Dialog::messageBox(ss.str());
}
template <typename... Args> [[noreturn]] void Exit(Args&&... args) {
    Log(std::forward<Args>(args)...);
    std::exit(1);
}
}

namespace String {
template <typename T>
inline std::u8string tou8(const std::basic_string<T>& str) {
    static_assert(sizeof(T) == sizeof(char8_t), "Invalid conversion.");
    return std::u8string(str.cbegin(), str.cend());
}
template <typename T>
inline std::u8string tou8(const std::basic_string_view<T> str) {
    static_assert(sizeof(T) == sizeof(char8_t), "Invalid conversion.");
    return std::u8string(str.cbegin(), str.cend());
}
#ifdef PLATFORM_WINDOWS
template <typename T>
inline std::basic_string<T> wideToMulti(const std::wstring_view wstr) {
    static_assert(sizeof(T) == sizeof(char), "Invalid conversion.");
    std::basic_string<T> str;
    int size = WideCharToMultiByte(
            CP_ACP, 0, wstr.data(), -1, nullptr, 0, nullptr, nullptr);
    str.resize(size-1, '\0');  // "size" includes padding for '\0'
    WideCharToMultiByte(CP_ACP, 0, wstr.data(), -1,
            reinterpret_cast<char *>(str.data()), size, nullptr, nullptr);
    return str;
}
#endif
}

namespace Enum {
template <typename T>
inline std::underlying_type_t<T> cast(T v) {
    return static_cast<decltype(cast(v))>(v);
}
}

// util.cpp
struct CmdArgs {
    using Path = std::filesystem::path;
    Path cwd;
    Path configFile;
    Path logFile;

    static CmdArgs Parse(const std::vector<std::string>& args);
};

namespace Yommd {
void slogFunc(const char *tag, uint32_t logLevel, uint32_t logItem,
        const char *message, uint32_t linenr, const char *filename, void *user_data);
void makeAbsolute(std::filesystem::path& path, const std::filesystem::path& cwd);
}

// config.cpp
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
    glm::vec2 defaultModelPosition;
    float defaultScale;
    glm::vec3 defaultCameraPosition;
    glm::vec3 defaultGazePosition;
    std::optional<int> defaultScreenNumber;

    static Config Parse(const std::filesystem::path& configFile);
};

// main_osx.mm main_windows.cpp
namespace Context {
sg_context_desc getSokolContext();
glm::vec2 getWindowSize();
glm::vec2 getDrawableSize();
glm::vec2 getMousePosition();
}

// resources.cpp
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
}

// image.cpp
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

// viewer.cpp
class Material {
public:
    explicit Material(const saba::MMDMaterial& mat);
    const saba::MMDMaterial& material;
    std::optional<sg_image> texture;
    std::optional<sg_image> spTexture;
    std::optional<sg_image> toonTexture;
    bool textureHasAlpha;
};

class MMD : private NonCopyable {
public:
    using Path = std::filesystem::path;
    using Animation = std::pair<
        std::unique_ptr<saba::VMDAnimation>,
        std::unique_ptr<saba::VMDCameraAnimation>>;
    void LoadModel(const Path& modelPath, const Path& resourcePath);
    void LoadMotion(const std::vector<Path>& paths);
    bool IsModelLoaded() const;
    const std::shared_ptr<saba::MMDModel> GetModel() const;
    const std::vector<Animation>& GetAnimations() const;
private:
    std::shared_ptr<saba::MMDModel> model_;
    std::vector<Animation> animations_;
};

class UserViewport {
public:
    UserViewport();
    glm::mat4 GetMatrix() const;
    operator glm::mat4() const;
    void OnMouseDown();
    void OnMouseDragged();
    void OnWheelScrolled(float delta);
    void SetDefaultTranslation(glm::vec2 pos);
    void SetDefaultScaling(float scale);
    void ResetPosition();
private:
    struct DragHelper {
        glm::vec2 firstMousePosition;
        glm::vec3 firstTranslate;
    };

    float scale_;
    glm::vec3 translate_;
    float defaultScale_;
    glm::vec3 defaultTranslate_;
    DragHelper dragHelper_;
};

class Routine : private NonCopyable {
public:
    Routine();
    ~Routine();
    void Init();
    void Update();
    void Draw();
    void Terminate();
    void OnMouseDragged();
    void OnMouseDown();
    void OnWheelScrolled(float delta);
    void ResetModelPosition();
    void ParseConfig(const CmdArgs& args);
    const Config& GetConfig() const;
private:
    using ImageMap = std::map<std::string, Image>;
    void initBuffers();
    void initTextures();
    void initPipeline();
    void selectNextMotion();
    std::optional<ImageMap::const_iterator> loadImage(const std::string& path);
    std::optional<sg_image> getTexture(const std::string& path);
private:
    struct Camera {
        glm::vec3 eye;
        glm::vec3 center;
    };

    Config config_;

    UserViewport userViewport_;

    bool shouldTerminate_;

    const sg_pass_action passAction_;
    sg_shader shaderMMD_;

    std::vector<uint32_t> induces_;
    sg_buffer posVB_;  // VB stands for vertex buffer
    sg_buffer normVB_;
    sg_buffer uvVB_;
    sg_buffer ibo_;
    sg_pipeline pipeline_frontface_;
    sg_pipeline pipeline_bothface_;
    sg_bindings binds_;

    glm::mat4 viewMatrix_;
    glm::mat4 projectionMatrix_;
    MMD mmd_;

    sg_image dummyTex_;
    ImageMap texImages_;
    std::map<std::string, sg_image> textures_;
    std::vector<Material> materials_;
    sg_sampler sampler_texture_;
    sg_sampler sampler_sphere_texture_;
    sg_sampler sampler_toon_texture_;

    Camera defaultCamera_;

    // Timers for animation.
    uint64_t timeBeginAnimation_;
    uint64_t timeLastFrame_;

    size_t motionID_;
    bool needBridgeMotions_;
    std::vector<unsigned int> motionWeights_;

    std::mt19937 rand_;
    std::uniform_int_distribution<size_t> randDist_;
};

#endif  // YOMMD_HPP_
