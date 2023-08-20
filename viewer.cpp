#include <ctime>
#include <filesystem>
#include <functional>
#include <numeric>
#include <optional>
#include <memory>
#include <string>
#include <string_view>
#include <random>
#include "sokol_gfx.h"
#include "sokol_time.h"
#include "Saba/Base/Path.h"
#include "Saba/Model/MMD/MMDCamera.h"
#include "Saba/Model/MMD/MMDMaterial.h"
#include "Saba/Model/MMD/MMDModel.h"
#include "Saba/Model/MMD/MMDPhysics.h"
#include "Saba/Model/MMD/PMDModel.h"
#include "Saba/Model/MMD/PMXModel.h"
#include "Saba/Model/MMD/VMDAnimation.h"
#include "Saba/Model/MMD/VMDCameraAnimation.h"
#include "Saba/Model/MMD/VMDFile.h"
#include "btBulletDynamicsCommon.h"
#include "viewer.hpp"
#include "main.hpp"
#include "constant.hpp"
#include "yommd.glsl.h"

namespace{
const std::filesystem::path getXdgConfigHomePath() {
#ifdef PLATFORM_WINDOWS
    const wchar_t *wpath = _wgetenv(L"XDG_CONFIG_HOME");
    if (wpath)
        return String::wideToMulti<char8_t>(wpath);
#else
    const char *path = std::getenv("XDG_CONFIG_HOME");
    if (path)
        return String::tou8(std::string_view(path));
#endif
    return "~/.config";
}
}

Material::Material(const saba::MMDMaterial& mat) :
    material(mat),
    textureHasAlpha(false)
{}

void MMD::LoadModel(
        const std::filesystem::path& modelPath,
        const std::filesystem::path& resourcePath) {
    const auto ext = std::filesystem::path(modelPath).extension();
    if (ext == ".pmx") {
        auto pmx = std::make_unique<saba::PMXModel>();
        if (!pmx->Load(modelPath.string(), resourcePath.string())) {
            Err::Exit("Failed to load PMX:", modelPath);
        }
        model_ = std::move(pmx);
    } else if (ext == ".pmd") {
        auto pmd = std::make_unique<saba::PMDModel>();
        if (!pmd->Load(modelPath.string(), resourcePath.string())) {
            Err::Exit("Failed to load PMD:", modelPath);
        }
        model_ = std::move(pmd);
    } else {
        Err::Exit("Unsupported MMD file:", modelPath);
    }

    model_->InitializeAnimation();
}

void MMD::LoadMotion(const std::vector<std::filesystem::path>& paths) {
    std::unique_ptr<saba::VMDCameraAnimation> cameraAnim(nullptr);
    auto vmdAnim = std::make_unique<saba::VMDAnimation>();
    if (!vmdAnim->Create(model_)) {
        Err::Exit("Failed to create VMDAnimation");
    }

    for (const auto& p : paths) {
        saba::VMDFile vmdFile;
        if (!saba::ReadVMDFile(&vmdFile, p.string().c_str())) {
            Err::Exit("Failed to read VMD file:", p);
        }
        if (!vmdAnim->Add(vmdFile)) {
            Err::Exit("Failed to add VMDAnimation:", p);
        }

        if (!vmdFile.m_cameras.empty()) {
            cameraAnim = std::make_unique<saba::VMDCameraAnimation>();
            if (!cameraAnim->Create(vmdFile))
                Err::Log("Failed to create VMDCameraAnimation:", p);
        }
    }

    animations_.push_back(std::make_pair(std::move(vmdAnim), std::move(cameraAnim)));
}

bool MMD::IsModelLoaded() const {
    return static_cast<bool>(model_);
}

const std::shared_ptr<saba::MMDModel> MMD::GetModel() const {
    return model_;
}

const std::vector<MMD::Animation>& MMD::GetAnimations() const {
    return animations_;
}

UserViewport::UserViewport() :
    scale_(1.0f), translate_(0.0f, 0.0f, 0.0f),
    defaultScale_(scale_), defaultTranslate_(translate_)
{}

glm::mat4 UserViewport::GetMatrix() const {
    const glm::vec3 scale(scale_, scale_, 1.0f);
    return glm::scale(glm::translate(glm::mat4(1.0f), translate_), scale);
}

UserViewport::operator glm::mat4() const {
    return GetMatrix();
}

void UserViewport::OnMouseDown() {
    dragHelper_.firstMousePosition = Context::getMousePosition();
    dragHelper_.firstTranslate = translate_;
}

void UserViewport::OnMouseDragged() {
    // const auto winSize{Context::getWindowSize()};
    // const float scale = (Context::getDrawableSize() / winSize).x;
    constexpr float scale = 2.0f;  // FIXME: Why this seems working well?
    auto delta = Context::getMousePosition() - dragHelper_.firstMousePosition;
    delta = delta / Context::getWindowSize() * scale;
    translate_.x = dragHelper_.firstTranslate.x + delta.x;
    translate_.y = dragHelper_.firstTranslate.y + delta.y;
}

void UserViewport::OnWheelScrolled(float delta) {
    scale_ -= delta / Context::getWindowSize().y;
    if (scale_ < 0.4f) {
        scale_ = 0.4f;
    }
}

void UserViewport::SetDefaultTranslation(glm::vec2 pos) {
    translate_.x = pos.x;
    translate_.y = -pos.y;
    defaultTranslate_ = translate_;
}

void UserViewport::SetDefaultScaling(float scale) {
    scale_ = scale;
    defaultScale_ = scale;
}

void UserViewport::ResetPosition() {
    scale_ = defaultScale_;
    translate_ = defaultTranslate_;
}

Routine::Routine() :
    passAction_({.colors = {{.load_action = SG_LOADACTION_CLEAR, .clear_value = {0, 0, 0, 0}}}}),
    binds_({}),
    timeBeginAnimation_(0), timeLastFrame_(0), motionID_(0), needBridgeMotions_(false),
    rand_(static_cast<int>(std::time(nullptr)))
{}

Routine::~Routine() {
    Terminate();
}

void Routine::Init() {
    namespace fs = std::filesystem;
    fs::path resourcePath = "<embedded-toons>";

    defaultCamera_.eye = config_.defaultCameraPosition;
    defaultCamera_.center = config_.defaultGazePosition;
    mmd_.LoadModel(config_.model, resourcePath);

    for (const auto& motion : config_.motions) {
        if (!motion.disabled) {
            mmd_.LoadMotion(motion.paths);
            motionWeights_.push_back(motion.weight);
        }
    }

    sg_desc desc = {
        .logger = {
            .func = Yommd::slogFunc,
        },
        .context = Context::getSokolContext(),
    };
    sg_setup(&desc);
    stm_setup();

    const sg_backend backend = sg_query_backend();
    shaderMMD_ = sg_make_shader(mmd_shader_desc(backend));

    initBuffers();
    initTextures();
    initPipeline();

    binds_.index_buffer = ibo_;
    binds_.vertex_buffers[ATTR_mmd_vs_in_Pos] = posVB_;
    binds_.vertex_buffers[ATTR_mmd_vs_in_Nor] = normVB_;
    binds_.vertex_buffers[ATTR_mmd_vs_in_UV] = uvVB_;

    const auto distSup = std::reduce(motionWeights_.cbegin(), motionWeights_.cend(), 0u);
    if (!motionWeights_.empty() && distSup == 0)
        Err::Exit("Sum of motion weights is 0.");
    randDist_.param(decltype(randDist_)::param_type(1, distSup));

    auto physics = mmd_.GetModel()->GetMMDPhysics();
    physics->GetDynamicsWorld()->setGravity(btVector3(0, -config_.gravity * 5.0f, 0));
    physics->SetMaxSubStepCount(INT_MAX);
    physics->SetFPS(config_.simulationFPS);

    userViewport_.SetDefaultTranslation(config_.defaultModelPosition);
    userViewport_.SetDefaultScaling(config_.defaultScale);

    selectNextMotion();
    needBridgeMotions_ = false;
    timeBeginAnimation_ = timeLastFrame_ = stm_now();
    shouldTerminate_ = true;
}

void Routine::initBuffers() {
    const auto model = mmd_.GetModel();
    const size_t vertCount = model->GetVertexCount();
    const size_t indexSize = model->GetIndexElementSize();

    posVB_ = sg_make_buffer(sg_buffer_desc{
                .size = vertCount * sizeof(glm::vec3),
                .type = SG_BUFFERTYPE_VERTEXBUFFER,
                .usage = SG_USAGE_DYNAMIC,
            });
    normVB_ = sg_make_buffer(sg_buffer_desc{
                .size = vertCount * sizeof(glm::vec3),
                .type = SG_BUFFERTYPE_VERTEXBUFFER,
                .usage = SG_USAGE_DYNAMIC,
            });
    uvVB_ = sg_make_buffer(sg_buffer_desc{
                .size = vertCount * sizeof(glm::vec2),
                .type = SG_BUFFERTYPE_VERTEXBUFFER,
                .usage = SG_USAGE_DYNAMIC,
            });


    // Prepare Index buffer object.
    const auto copyInduces = [&model, this](const auto *mmdInduces) {
        const size_t subMeshCount = model->GetSubMeshCount();
        for (size_t i = 0; i < subMeshCount; ++i) {
            const auto& subMesth = model->GetSubMeshes()[i];
            for (int j = 0; j < subMesth.m_vertexCount; ++j)
                induces_.push_back(
                        static_cast<uint32_t>(mmdInduces[subMesth.m_beginIndex + j]));
        }
    };
    switch (indexSize) {
    case 1:
        copyInduces(static_cast<const uint8_t *>(model->GetIndices()));
        break;
    case 2:
        copyInduces(static_cast<const uint16_t *>(model->GetIndices()));
        break;
    case 4:
        copyInduces(static_cast<const uint32_t *>(model->GetIndices()));
        break;
    default:
        Err::Exit("Maybe MMD data is broken: indexSize:", indexSize);
    }

    ibo_ = sg_make_buffer(sg_buffer_desc{
                .type = SG_BUFFERTYPE_INDEXBUFFER,
                .usage = SG_USAGE_IMMUTABLE,
                .data = {
                    .ptr = induces_.data(),
                    .size = induces_.size() * sizeof(uint32_t),
                },
            });
}

void Routine::initTextures() {
    static constexpr uint8_t dummyPixel[4] = {0, 0, 0, 0};

    dummyTex_ = sg_make_image(sg_image_desc{
        .width = 1,
        .height = 1,
        .data = {
            .subimage = {{{.ptr = dummyPixel, .size = 4}}},
        },
    });

    const auto& model = mmd_.GetModel();
    const size_t subMeshCount = model->GetSubMeshCount();
    for (size_t i = 0; i < subMeshCount; ++i) {
        const auto& mmdMaterial = model->GetMaterials()[i];
        Material material(mmdMaterial);
        if (!mmdMaterial.m_texture.empty()) {
            material.texture = getTexture(mmdMaterial.m_texture);
            if (material.texture) {
                material.textureHasAlpha = texImages_[mmdMaterial.m_texture].hasAlpha;
            }
        }
        if (!mmdMaterial.m_spTexture.empty()) {
            material.spTexture = getTexture(mmdMaterial.m_spTexture);
        }
        if (!mmdMaterial.m_toonTexture.empty()) {
            material.toonTexture = getTexture(mmdMaterial.m_toonTexture);
        }
        materials_.push_back(std::move(material));
    }

    sampler_texture_ = sg_make_sampler(sg_sampler_desc{
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
    });
    sampler_sphere_texture_ = sg_make_sampler(sg_sampler_desc{
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
    });
    sampler_toon_texture_ = sg_make_sampler(sg_sampler_desc{
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
    });
}

void Routine::initPipeline() {
    sg_vertex_layout_state layout_desc;
    layout_desc.attrs[ATTR_mmd_vs_in_Pos] = {
        .buffer_index = ATTR_mmd_vs_in_Pos,
        .format = SG_VERTEXFORMAT_FLOAT3,
    };
    layout_desc.attrs[ATTR_mmd_vs_in_Nor] = {
        .buffer_index = ATTR_mmd_vs_in_Nor,
        .format = SG_VERTEXFORMAT_FLOAT3,
    };
    layout_desc.attrs[ATTR_mmd_vs_in_UV] = {
        .buffer_index = ATTR_mmd_vs_in_UV,
        .format = SG_VERTEXFORMAT_FLOAT2,
    };

    sg_color_target_state color_state = {
        .blend = {
            .enabled = true,
            .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
            .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .src_factor_alpha = SG_BLENDFACTOR_ONE,
            .dst_factor_alpha = SG_BLENDFACTOR_ONE,
        },
    };

    sg_pipeline_desc pipeline_desc = {
        .shader = shaderMMD_,
        .depth = {
            .compare = SG_COMPAREFUNC_LESS_EQUAL,  // FIXME: SG_COMPAREFUNC_LESS?
            .write_enabled = true,
        },
        .colors = {{color_state}},
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
        .index_type = SG_INDEXTYPE_UINT32,
        .cull_mode = SG_CULLMODE_FRONT,
        .face_winding = SG_FACEWINDING_CW,
        .sample_count = Constant::SampleCount,
    };
    pipeline_desc.layout.attrs[ATTR_mmd_vs_in_Pos] = layout_desc.attrs[ATTR_mmd_vs_in_Pos];
    pipeline_desc.layout.attrs[ATTR_mmd_vs_in_Nor] = layout_desc.attrs[ATTR_mmd_vs_in_Nor];
    pipeline_desc.layout.attrs[ATTR_mmd_vs_in_UV] = layout_desc.attrs[ATTR_mmd_vs_in_UV];

    pipeline_frontface_ = sg_make_pipeline(&pipeline_desc);

    pipeline_desc.cull_mode = SG_CULLMODE_NONE;
    pipeline_bothface_ = sg_make_pipeline(&pipeline_desc);
}

void Routine::selectNextMotion() {
    // Select next MMD motion by weighted rate.
    if (motionWeights_.empty())
        return;

    unsigned int rnd = randDist_(rand_);
    unsigned int sum = 0;
    const auto motionCount = motionWeights_.size();
    motionID_ = motionCount - 1;
    for (size_t i = 0; i < motionCount; ++i) {
        sum += motionWeights_[i];
        if (sum >= rnd) {
            motionID_ = i;
            break;
        }
    }
    if (motionID_ >= motionCount)
        Err::Exit("Internal error: unreachable:", __FILE__ ":", __LINE__, ':', __func__);
}

void Routine::Update() {
    const auto size{Context::getWindowSize()};
    const auto model = mmd_.GetModel();
    const size_t vertCount = model->GetVertexCount();
    const double vmdFrame = stm_sec(stm_since(timeBeginAnimation_)) * Constant::VmdFPS;
    const double elapsedTime = stm_sec(stm_since(timeLastFrame_));

    auto& animations = mmd_.GetAnimations();

    if (!animations.empty()) {
        // Update camera animation.
        auto& [vmdAnim, cameraAnim] = animations[motionID_];
        if (cameraAnim) {
            cameraAnim->Evaluate(vmdFrame);
            const auto& mmdCamera = cameraAnim->GetCamera();
            saba::MMDLookAtCamera lookAtCamera(mmdCamera);
            viewMatrix_ = glm::lookAt(
                    lookAtCamera.m_eye,
                    lookAtCamera.m_center,
                    lookAtCamera.m_up);
            projectionMatrix_ = glm::perspectiveFovRH(
                    mmdCamera.m_fov,
                    static_cast<float>(size.x),
                    static_cast<float>(size.y),
                    1.0f,
                    10000.0f);
        } else {
            viewMatrix_ = glm::lookAt(
                    defaultCamera_.eye,
                    defaultCamera_.center,
                    glm::vec3(0, 1, 0));
            projectionMatrix_ = glm::perspectiveFovRH(
                    glm::radians(30.0f),
                    static_cast<float>(size.x),
                    static_cast<float>(size.y),
                    1.0f,
                    10000.0f);
        }

        model->BeginAnimation();
        if (needBridgeMotions_) {
            vmdAnim->Evaluate(0.0f, stm_sec(stm_since(timeBeginAnimation_)));
            model->UpdateMorphAnimation();
            model->UpdateNodeAnimation(false);
            model->UpdatePhysicsAnimation(elapsedTime);
            model->UpdateNodeAnimation(true);
            if (vmdFrame >= Constant::VmdFPS) {
                needBridgeMotions_ = false;
                timeBeginAnimation_ = stm_now();
            }
        } else {
            model->UpdateAllAnimation(vmdAnim.get(), vmdFrame, elapsedTime);
        }
        model->EndAnimation();
    }
    model->Update();

    sg_update_buffer(posVB_, sg_range{
                .ptr = model->GetUpdatePositions(),
                .size = vertCount * sizeof(glm::vec3),
            });
    sg_update_buffer(normVB_, sg_range{
                .ptr = model->GetUpdateNormals(),
                .size = vertCount * sizeof(glm::vec3),
            });
    sg_update_buffer(uvVB_, sg_range{
                .ptr = model->GetUpdateUVs(),
                .size = vertCount * sizeof(glm::vec2),
            });

    if (!animations.empty()) {
        auto& vmdAnim = animations[motionID_].first;
        timeLastFrame_ = stm_now();
        if (vmdFrame > vmdAnim->GetMaxKeyTime()) {
            model->SaveBaseAnimation();
            timeBeginAnimation_ = timeLastFrame_;
            selectNextMotion();
            needBridgeMotions_ = true;
        }
    }
}

void Routine::Draw() {
    const auto size{Context::getWindowSize()};
    const auto model = mmd_.GetModel();
    // const auto& dxMat = glm::mat4(
    //     1.0f, 0.0f, 0.0f, 0.0f,
    //     0.0f, 1.0f, 0.0f, 0.0f,
    //     0.0f, 0.0f, 0.5f, 0.0f,
    //     0.0f, 0.0f, 0.5f, 1.0f
    // );

    auto userView = userViewport_.GetMatrix();
    auto world = glm::mat4(1.0f);
    auto wv = userView * viewMatrix_ * world;
    auto wvp = userView * projectionMatrix_ * viewMatrix_ * world;
    // wvp = dxMat * wvp;
    auto wvit = glm::mat3(userView * viewMatrix_ * world);
    wvit = glm::inverse(wvit);
    wvit = glm::transpose(wvit);

    auto lightColor = glm::vec3(1, 1, 1);
    auto lightDir = glm::vec3(-0.5f, -1.0f, -0.5f);
    // auto viewMat = glm::mat3(viewMatrix);
    lightDir = glm::mat3(viewMatrix_) * lightDir;

    sg_begin_default_pass(&passAction_, size.x, size.y);

    const size_t subMeshCount = model->GetSubMeshCount();
    for (size_t i = 0; i < subMeshCount; ++i) {
        const auto& subMesh = model->GetSubMeshes()[i];
        const auto & material = materials_[subMesh.m_materialID];
        const auto& mmdMaterial = material.material;

        if (mmdMaterial.m_alpha == 0)
            continue;

        u_mmd_vs_t u_mmd_vs = {
            .u_WV = wv,
            .u_WVP = wvp,
        };

        u_mmd_fs_t u_mmd_fs = {
            .u_Alpha = mmdMaterial.m_alpha,
            .u_Diffuse = mmdMaterial.m_diffuse,
            .u_Ambient = mmdMaterial.m_ambient,
            .u_Specular = mmdMaterial.m_specular,
            .u_SpecularPower = mmdMaterial.m_specularPower,
            .u_LightColor = lightColor,
            .u_LightDir = lightDir,
            .u_TexMode = 0,
            .u_ToonTexMode = 0,
            .u_SphereTexMode = 0,
        };

        if (material.texture) {
            binds_.fs.images[SLOT_u_Tex] = *material.texture;
            binds_.fs.samplers[SLOT_u_Tex_smp] = sampler_texture_;
            if (material.textureHasAlpha) {
                // Use Material Alpha * Texture Alpha
                u_mmd_fs.u_TexMode = 2;
            } else {
                // Use Material Alpha
                u_mmd_fs.u_TexMode = 1;
            }
            u_mmd_fs.u_TexMulFactor = mmdMaterial.m_textureMulFactor;
            u_mmd_fs.u_TexAddFactor = mmdMaterial.m_textureAddFactor;
        } else {
            binds_.fs.images[SLOT_u_Tex] = dummyTex_;
            binds_.fs.samplers[SLOT_u_Tex_smp] = sampler_texture_;
        }

        if (material.spTexture) {
            binds_.fs.images[SLOT_u_SphereTex] = *material.spTexture;
            binds_.fs.samplers[SLOT_u_SphereTex_smp] = sampler_sphere_texture_;
            switch (mmdMaterial.m_spTextureMode) {
            case saba::MMDMaterial::SphereTextureMode::Mul:
                u_mmd_fs.u_SphereTexMode = 1;
                break;
            case saba::MMDMaterial::SphereTextureMode::Add:
                u_mmd_fs.u_SphereTexMode = 2;
                break;
            default:
                break;
            }
            u_mmd_fs.u_SphereTexMulFactor = mmdMaterial.m_spTextureMulFactor;
            u_mmd_fs.u_SphereTexAddFactor = mmdMaterial.m_spTextureAddFactor;
        } else {
            binds_.fs.images[SLOT_u_SphereTex] = dummyTex_;
            binds_.fs.samplers[SLOT_u_SphereTex_smp] = sampler_sphere_texture_;
        }

        if (material.toonTexture) {
            binds_.fs.images[SLOT_u_ToonTex] = *material.toonTexture;
            binds_.fs.samplers[SLOT_u_ToonTex_smp] = sampler_toon_texture_;
            u_mmd_fs.u_ToonTexMulFactor = mmdMaterial.m_toonTextureMulFactor;
            u_mmd_fs.u_ToonTexAddFactor = mmdMaterial.m_toonTextureAddFactor;
            u_mmd_fs.u_ToonTexMode = 1;
        } else {
            binds_.fs.images[SLOT_u_ToonTex] = dummyTex_;
            binds_.fs.samplers[SLOT_u_ToonTex_smp] = sampler_toon_texture_;
        }

        if (mmdMaterial.m_bothFace)
            sg_apply_pipeline(pipeline_bothface_);
        else
            sg_apply_pipeline(pipeline_frontface_);
        sg_apply_bindings(binds_);
        sg_apply_uniforms(SG_SHADERSTAGE_VS, SLOT_u_mmd_vs, SG_RANGE(u_mmd_vs));
        sg_apply_uniforms(SG_SHADERSTAGE_FS, SLOT_u_mmd_fs, SG_RANGE(u_mmd_fs));

        sg_draw(subMesh.m_beginIndex, subMesh.m_vertexCount, 1);
    }

    sg_end_pass();
    sg_commit();
}

void Routine::Terminate() {
    if (!shouldTerminate_)
        return;

    motionID_ = 0;
    motionWeights_.clear();
    induces_.clear();
    texImages_.clear();
    textures_.clear();
    materials_.clear();

    sg_destroy_shader(shaderMMD_);

    sg_destroy_buffer(posVB_);
    sg_destroy_buffer(normVB_);
    sg_destroy_buffer(uvVB_);

    sg_destroy_image(dummyTex_);

    sg_destroy_pipeline(pipeline_frontface_);
    sg_destroy_pipeline(pipeline_bothface_);

    sg_shutdown();

    shouldTerminate_ = false;
}

void Routine::OnMouseDown() {
    userViewport_.OnMouseDown();
}

void Routine::OnMouseDragged() {
    userViewport_.OnMouseDragged();
}

void Routine::OnWheelScrolled(float delta) {
    userViewport_.OnWheelScrolled(delta);
}

void Routine::ResetModelPosition() {
    userViewport_.ResetPosition();
}

void Routine::ParseConfig(const CmdArgs& args) {
    namespace fs = std::filesystem;
    fs::path configFile = args.configFile;
    if (configFile.empty()) {
        fs::path paths[] = {
            "./config.toml",
            getXdgConfigHomePath() / "yoMMD/config.toml",
            "~/yoMMD/config.toml",
        };
        for (auto& file : paths) {
            Yommd::makeAbsolute(file, args.cwd);
            if (fs::exists(file)) {
                configFile = file;
                break;
            }
        }
    }
    if (configFile.empty())
        Err::Exit("No config file found.");

    config_ = Config::Parse(configFile);
}

const Config& Routine::GetConfig() const {
    return config_;
}

std::optional<Routine::ImageMap::const_iterator> Routine::loadImage(const std::string& path) {
    auto itr = texImages_.find(path);
    if (itr == texImages_.cend()) {
        Image img;
        if (path.starts_with("<embedded-toons>")) {
            if (img.loadFromMemory(Resource::getToonData(path))) {
                texImages_.emplace(path, std::move(img));
                return texImages_.find(path);
            }
        } else if (img.loadFromFile(path)) {
            texImages_.emplace(path, std::move(img));
            return texImages_.find(path);
        }
        return std::nullopt;
    } else {
        return itr;
    }
}

std::optional<sg_image> Routine::getTexture(const std::string& path) {
    if (const auto itr = textures_.find(path); itr != textures_.cend())
        return itr->second;

    const auto itr = loadImage(path);
    if (!itr)
        return std::nullopt;

    const auto& image = (*itr)->second;
    sg_image_desc image_desc = {
        .type = SG_IMAGETYPE_2D,
        .render_target = false,
        .width = static_cast<int>(image.width),
        .height = static_cast<int>(image.height),
        .usage = SG_USAGE_IMMUTABLE,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
    };
    image_desc.data.subimage[0][0] =  {
        .ptr = image.pixels.data(),
        .size = image.pixels.size(),
    };
    const sg_image handler = sg_make_image(&image_desc);
    textures_.emplace(path, handler);
    return handler;
}
