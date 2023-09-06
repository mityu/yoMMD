#ifndef VIEWER_HPP_
#define VIEWER_HPP_

#include <random>
#include <map>
#include <filesystem>
#include "Saba/Model/MMD/MMDMaterial.h"
#include "Saba/Model/MMD/MMDModel.h"
#include "Saba/Model/MMD/VMDAnimation.h"
#include "Saba/Model/MMD/VMDCameraAnimation.h"
#include "sokol_gfx.h"
#include "util.hpp"
#include "config.hpp"
#include "image.hpp"

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

enum class GesturePhase {
    Unknown,
    Begin,
    Ongoing,
    End,
};

class UserViewport {
public:
    UserViewport();
    glm::mat4 GetMatrix() const;
    operator glm::mat4() const;
    void OnMouseDown();
    void OnMouseDragged();
    void OnWheelScrolled(float delta);
    void OnGestureZoom(GesturePhase phase, float delta);
    void SetDefaultTranslation(glm::vec2 pos);
    void SetDefaultScaling(float scale);
    void ResetPosition();
    float GetScale() const;
private:
    static bool isDifferentPoint(const glm::vec2& p1, const glm::vec2& p2);

    // changeScale changes the scale of MMD model to "newScale".  The base
    // point of scaling is the "refpoint", which specified in the coodinate on
    // screen.
    void changeScale(float newScale, glm::vec2 refpoint);
private:
    struct DragHelper {
        glm::vec2 firstMousePosition;
        glm::vec3 firstTranslate;
    };
    struct ScalingHelper {
        float firstScale = 0.0f;
        glm::vec2 firstRefpoint;
        glm::vec3 firstTranslate;
    };

    float scale_;
    glm::vec3 translate_;
    float defaultScale_;
    glm::vec3 defaultTranslate_;
    DragHelper dragHelper_;
    ScalingHelper scalingHelper_;
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
    void OnGestureZoom(GesturePhase phase, float delta);
    float GetModelScale() const;
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



#endif  // VIEWER_HPP_
