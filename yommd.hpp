#ifndef YOMMD_HPP_
#define YOMMD_HPP_

#include <optional>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include <iostream>
#include <cstdlib>
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
constexpr std::string_view DefaultConfigFilePath = "./config.toml";
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

namespace Err {
template <typename... Args> void Log(Args&&... args) {
    ::_internal::_log(std::cerr, std::forward<Args>(args)...);
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
    int size = WideCharToMultibyte(
            CP_ACP, 0, wstr.data(), -1, nullptr, 0, nullptr, nullptr);
    path.resize(size - 1);  // "size" includes padding for '\0'
    WideCharToMultiByte(CP_ACP, 0, wstr.data(), -1,
            str.data(), size, nullptr, nullptr);
    return str;
}
#endif
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
        bool enabled;
        unsigned int weight;
        Path path;
    };
    Config();

    Path model;
    std::vector<Motion> motions;
    float simulationFPS;
    float gravity;
    glm::vec2 defaultPosition;
    float defaultScale;

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
    void Load(const Path& modelPath,
            const std::vector<const Path *>& motionPaths,  // We doesn't have path_view yet.
            const Path& resourcePath);
    bool IsLoaded() const;
    const std::shared_ptr<saba::MMDModel> GetModel() const;
    const std::vector<std::unique_ptr<saba::VMDAnimation>>& GetAnimations() const;
    const std::unique_ptr<saba::VMDCameraAnimation>& GetCameraAnimation() const;
private:
    std::shared_ptr<saba::MMDModel> model;
    std::vector<std::unique_ptr<saba::VMDAnimation>> animations;
    std::unique_ptr<saba::VMDCameraAnimation> cameraAnimation;
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
    void Init(const CmdArgs &args);
    void Update();
    void Draw();
    void Terminate();
    void OnMouseDragged();
    void OnMouseDown();
    void OnWheelScrolled(float delta);
    void ResetModelPosition();
private:
    using ImageMap = std::map<std::string, Image>;
    void initBuffers();
    void initTextures();
    void initPipeline();
    void selectNextMotion();
    std::optional<ImageMap::const_iterator> loadImage(const std::string& path);
    std::optional<sg_image> getTexture(const std::string& path);
    std::optional<sg_image> getToonTexture(const std::string& path);
private:
    UserViewport userViewport_;

    bool shouldTerminate;

    const sg_pass_action passAction;
    sg_shader shaderMMD;

    std::vector<uint32_t> induces;
    sg_buffer posVB;  // VB stands for vertex buffer
    sg_buffer normVB;
    sg_buffer uvVB;
    sg_buffer ibo;
    sg_pipeline pipeline_frontface;
    sg_pipeline pipeline_bothface;
    sg_bindings binds;

    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
    MMD mmd;

    sg_image dummyTex;
    ImageMap texImages;
    std::map<std::string, sg_image> textures;  // For "texture" and "spTexture".
    std::map<std::string, sg_image> toonTextures;
    std::vector<Material> materials;

    // Timers for animation.
    uint64_t timeBeginAnimation;
    uint64_t timeLastFrame;

    size_t motionID;
    bool needBridgeMotions;
    std::vector<unsigned int> motionWeights;

    std::mt19937 rand;
    std::uniform_int_distribution<size_t> randDist;
};

#endif  // YOMMD_HPP_
