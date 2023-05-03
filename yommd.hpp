#ifndef YOMMD_HPP_
#define YOMMD_HPP_

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

#if defined(PLATFORM_WINDOWS) && !defined(_WIN32)
#  define _WIN32
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
}

// config.cpp
struct Config {
    struct Motion {
        bool enabled;
        unsigned int weight;
        std::string path;
    };
    Config();

    std::string model;
    std::vector<Motion> motions;
    float simulationFPS;
    float gravity;
    glm::vec2 defaultPosition;
    float defaultScale;

    static Config Parse(std::string_view configFile);
};

// main_osx.mm
namespace Context {
sg_context_desc getSokolContext();
glm::vec2 getWindowSize();
glm::vec2 getDrawableSize();
glm::vec2 getMousePosition();
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
    void Load(std::string_view modelPath,
            const std::vector<std::string>& motionPaths,
            std::string_view resourcePath);
    bool IsLoaded() const;
    const std::shared_ptr<saba::MMDModel> GetModel() const;
    const std::vector<std::unique_ptr<saba::VMDAnimation>>& GetAnimations() const;
    const std::unique_ptr<saba::VMDCameraAnimation>& GetCameraAnimation() const;
private:
    std::shared_ptr<saba::MMDModel> model;
    std::vector<std::unique_ptr<saba::VMDAnimation>> animations;
    std::unique_ptr<saba::VMDCameraAnimation> cameraAnimation;
};

// class TransparentFBO {
// public:
//     TransparentFBO();
//     ~TransparentFBO();
//     void Init();
//     void Draw();
//     void Terminate();
// private:
//     bool shouldTerminate;
//     sg_pass transparentFbo;
//     sg_pass transparentMSAAFbo;
//     sg_image transparentFboColorTex;
//     sg_image transparentFboMSAAColorRB;
//     sg_image transparentFboMSAADepthRB;
//     sg_shader shaderCopy;
// #ifdef PLATFORM_WINDOWS
//     sg_shader shaderCopyTransparentWindow;  // TODO: Use shaderCopy too.
// #endif
//     sg_pipeline pipeline;
//     sg_bindings binds;
// };

class UserViewport {
public:
    UserViewport();
    glm::mat4 getMatrix() const;
    operator glm::mat4() const;
    void onMouseDown();
    void onMouseDragged();
    void onWheelScrolled(float delta);
    void setDefaultTranslation(glm::vec2 pos);
    void setDefaultScaling(float scale);
private:
    struct DragHelper {
        glm::vec2 firstMousePosition;
        glm::vec3 firstTranslate;
    };

    float scale_;
    glm::vec3 translate_;
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
private:
    using ImageMap = std::map<std::string, Image>;
    void initBuffers();
    void initTextures();
    void initPipeline();
    void selectNextMotion();
    std::optional<ImageMap::const_iterator> loadImage(std::string path);
    std::optional<sg_image> getTexture(std::string path);
    std::optional<sg_image> getToonTexture(std::string path);
private:
    UserViewport userViewport_;

    bool shouldTerminate;

    const sg_pass_action passAction;
    sg_shader shaderMMD;

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
    // TransparentFBO transparentFBO;

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
