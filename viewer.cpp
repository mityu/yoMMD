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
#include "yommd.hpp"
#include "yommd.glsl.h"


Material::Material(const saba::MMDMaterial& mat) :
    material(mat),
    textureHasAlpha(false)
{}

void MMD::Load(
        std::string_view modelPath,
        const std::vector<std::string>& motionPaths,
        std::string_view resourcePath) {
    const auto ext = std::filesystem::path(modelPath).extension();
    if (ext == ".pmx") {
        auto pmx = std::make_unique<saba::PMXModel>();
        if (!pmx->Load(std::string(modelPath), std::string(resourcePath))) {
            Err::Exit("Failed to load PMX:", modelPath);
        }
        model = std::move(pmx);
    } else if (ext == ".pmd") {
        auto pmd = std::make_unique<saba::PMDModel>();
        if (!pmd->Load(std::string(modelPath), std::string(resourcePath))) {
            Err::Exit("Failed to load PMD:", modelPath);
        }
        model = std::move(pmd);
    } else {
        Err::Exit("Unsupported MMD file:", modelPath);
    }

    model->InitializeAnimation();

    for (const auto& motionPath : motionPaths) {
        auto vmdAnim = std::make_unique<saba::VMDAnimation>();
        if (!vmdAnim->Create(model)) {
            Err::Exit("Failed to create VMDAnimation:", motionPath);
        }
        saba::VMDFile vmdFile;
        if (!saba::ReadVMDFile(&vmdFile, motionPath.data())) {
            Err::Exit("Failed to read VMD file:", motionPath);
        }
        if (!vmdAnim->Add(vmdFile)) {
            Err::Exit("Failed to add VMDAnimation:", motionPath);
        }

        if (!vmdFile.m_cameras.empty()) {
            cameraAnimation = std::make_unique<saba::VMDCameraAnimation>();
            if (!cameraAnimation->Create(vmdFile))
                Err::Log("Failed to create VMDCameraAnimation:", motionPath);
        }

        animations.push_back(std::move(vmdAnim));
    }
}

bool MMD::IsLoaded() const {
    return static_cast<bool>(model);
}

const std::shared_ptr<saba::MMDModel> MMD::GetModel() const {
    return model;
}

const std::vector<std::unique_ptr<saba::VMDAnimation>>& MMD::GetAnimations() const {
    return animations;
}

const std::unique_ptr<saba::VMDCameraAnimation>& MMD::GetCameraAnimation() const {
    return cameraAnimation;
}

UserViewport::UserViewport() :
    scale_(1.0f), translate_(0.0f, 0.0f, 0.0f)
{}

glm::mat4 UserViewport::getMatrix() const {
    const glm::vec3 scale(scale_, scale_, 1.0f);
    return glm::scale(glm::translate(glm::mat4(1.0f), translate_), scale);
}

UserViewport::operator glm::mat4() const {
    return getMatrix();
}

void UserViewport::onMouseDown() {
    dragHelper_.firstMousePosition = Context::getMousePosition();
    dragHelper_.firstTranslate = translate_;
}

void UserViewport::onMouseDragged() {
    const auto winSize{Context::getWindowSize()};
    const float scale = (Context::getDrawableSize() / winSize).x;
    auto delta = Context::getMousePosition() - dragHelper_.firstMousePosition;
    delta = delta / Context::getWindowSize() * scale;
    translate_.x = dragHelper_.firstTranslate.x + delta.x;
    translate_.y = dragHelper_.firstTranslate.y + delta.y;
}

void UserViewport::onWheelScrolled(float delta) {
    scale_ -= delta / Context::getWindowSize().y;
    if (scale_ < 0.4f) {
        scale_ = 0.4f;
    }
}

void UserViewport::setDefaultTranslation(glm::vec2 pos) {
    translate_.x = pos.x;
    translate_.y = -pos.y;
}

void UserViewport::setDefaultScaling(float scale) {
    scale_ = scale;
}

Routine::Routine() :
    passAction({.colors[0] = {.action = SG_ACTION_CLEAR, .value = {1, 1, 1, 0}}}),
    timeBeginAnimation(0), timeLastFrame(0), motionID(0), needBridgeMotions(false),
    rand(static_cast<int>(std::time(nullptr)))
{}

Routine::~Routine() {
    Terminate();
}

void Routine::Init(const CmdArgs& args) {
    std::filesystem::path resourcePath = args.cwd / "./misc/resource/mmd";
    std::vector<std::string> motionPaths;
    const Config config = Config::Parse(args.configFile.string());
    for (const auto& motion : config.motions) {
        if (motion.enabled) {
            std::filesystem::path path(motion.path);
            if (!path.is_absolute())
                path = args.cwd / path;
            motionPaths.push_back(path);
            motionWeights.push_back(motion.weight);
        }
    }
    if (motionPaths.empty())
        Err::Exit("No motion file specified/enabled");  // FIXME: Allow only view MMD model
    mmd.Load(config.model, motionPaths, resourcePath.string());

    sg_desc desc = {
        .context = Context::getSokolContext(),
        .logger.func = Yommd::slogFunc,
    };
    sg_setup(&desc);
    stm_setup();

    const sg_backend backend = sg_query_backend();
    shaderMMD = sg_make_shader(mmd_shader_desc(backend));

    initBuffers();
    initTextures();
    initPipeline();

    binds = sg_bindings{
        .vertex_buffers[ATTR_mmd_vs_in_Pos] = posVB,
        .vertex_buffers[ATTR_mmd_vs_in_Nor] = normVB,
        .vertex_buffers[ATTR_mmd_vs_in_UV] = uvVB,
        .index_buffer = ibo,
    };

    const auto distSup = std::reduce(motionWeights.cbegin(), motionWeights.cend(), 0u);
    if (!motionWeights.empty() && distSup == 0)
        Err::Exit("Sum of motion weights is 0.");
    randDist.param(decltype(randDist)::param_type(0, distSup - 1));

    auto physics = mmd.GetModel()->GetMMDPhysics();
    physics->GetDynamicsWorld()->setGravity(btVector3(0, -9.8f * 5.0f, 0));
    physics->SetMaxSubStepCount(INT_MAX);
    physics->SetFPS(config.simulationFPS);

    userViewport_.setDefaultTranslation(config.defaultPosition);
    userViewport_.setDefaultScaling(config.defaultScale);

    selectNextMotion();
    needBridgeMotions = false;
    timeBeginAnimation = timeLastFrame = stm_now();
    shouldTerminate = true;
}

void Routine::initBuffers() {
    const auto model = mmd.GetModel();
    const size_t vertCount = model->GetVertexCount();
    const size_t indexSize = model->GetIndexElementSize();

    posVB = sg_make_buffer(sg_buffer_desc{
                .type = SG_BUFFERTYPE_VERTEXBUFFER,
                .usage = SG_USAGE_DYNAMIC,
                .size = vertCount * sizeof(glm::vec3),
            });
    normVB = sg_make_buffer(sg_buffer_desc{
                .type = SG_BUFFERTYPE_VERTEXBUFFER,
                .usage = SG_USAGE_DYNAMIC,
                .size = vertCount * sizeof(glm::vec3),
            });
    uvVB = sg_make_buffer(sg_buffer_desc{
                .type = SG_BUFFERTYPE_VERTEXBUFFER,
                .usage = SG_USAGE_DYNAMIC,
                .size = vertCount * sizeof(glm::vec2),
            });


    // Prepare Index buffer object.
    std::function<uint32_t(size_t)> getInduceAt;
    switch (indexSize) {
    case 1: {
        const uint8_t *mmdInduces = static_cast<const uint8_t *>(model->GetIndices());
        getInduceAt = [mmdInduces](size_t i) -> uint32_t {
            return static_cast<uint32_t>(mmdInduces[i]);
        };
        break;
    }
    case 2: {
        const uint16_t *mmdInduces = static_cast<const uint16_t *>(model->GetIndices());
        getInduceAt = [mmdInduces](size_t i) -> uint32_t {
            return static_cast<uint32_t>(mmdInduces[i]);
        };
        break;
    }
    case 4: {
        const uint32_t *mmdInduces = static_cast<const uint32_t *>(model->GetIndices());
        getInduceAt = [mmdInduces](size_t i) -> uint32_t {
            return mmdInduces[i];
        };
        break;
    }
    default:
        Err::Exit("Maybe MMD data is broken: indexSize:", indexSize);
    }

    const size_t subMeshCount = model->GetSubMeshCount();
    for (size_t i = 0; i < subMeshCount; ++i) {
        const auto subMesth = model->GetSubMeshes()[i];
        for (int j = 0; j < subMesth.m_vertexCount; ++j)
            induces.push_back(getInduceAt(subMesth.m_beginIndex + j));
    }

    ibo = sg_make_buffer(sg_buffer_desc{
                .type = SG_BUFFERTYPE_INDEXBUFFER,
                .usage = SG_USAGE_IMMUTABLE,
                .data = {
                    .size = induces.size() * sizeof(uint32_t),
                    .ptr = induces.data(),
                },
            });
}

void Routine::initTextures() {
    static constexpr uint8_t dummyPixel[4] = {0, 0, 0, 0};

    dummyTex = sg_make_image(sg_image_desc{
                .width = 1,
                .height = 1,
                .data.subimage[0][0] = {.size = 4, .ptr = dummyPixel},
            });

    const auto& model = mmd.GetModel();
    const size_t subMeshCount = model->GetSubMeshCount();
    for (size_t i = 0; i < subMeshCount; ++i) {
        const auto& mmdMaterial = model->GetMaterials()[i];
        Material material(mmdMaterial);
        if (!mmdMaterial.m_texture.empty()) {
            material.texture = getTexture(mmdMaterial.m_texture);
            if (material.texture) {
                material.textureHasAlpha = texImages[mmdMaterial.m_texture].hasAlpha;
            }
        }
        if (!mmdMaterial.m_spTexture.empty()) {
            material.spTexture = getTexture(mmdMaterial.m_spTexture);
        }
        if (!mmdMaterial.m_toonTexture.empty()) {
            material.toonTexture = getToonTexture(mmdMaterial.m_toonTexture);
        }
        materials.push_back(std::move(material));
    }
}

void Routine::initPipeline() {
    sg_pipeline_desc pipeline_desc = {
        .shader = shaderMMD,
        .index_type = SG_INDEXTYPE_UINT32,
        .layout = {
            .attrs = {
                [ATTR_mmd_vs_in_Pos] = {
                    .buffer_index = ATTR_mmd_vs_in_Pos,
                    .format = SG_VERTEXFORMAT_FLOAT3,
                },
                [ATTR_mmd_vs_in_Nor] = {
                    .buffer_index = ATTR_mmd_vs_in_Nor,
                    .format = SG_VERTEXFORMAT_FLOAT3,
                },
                [ATTR_mmd_vs_in_UV] = {
                    .buffer_index = ATTR_mmd_vs_in_UV,
                    .format = SG_VERTEXFORMAT_FLOAT2,
                },
            },
        },
        .cull_mode = SG_CULLMODE_FRONT,
        .face_winding = SG_FACEWINDING_CW,
        // .stencil = {
        //     .enabled = true,
        //     .front = {
        //         // .compare = SG_COMPAREFUNC_NOT_EQUAL,
        //         // .pass_op = SG_STENCILOP_REPLACE,
        //         .compare = SG_COMPAREFUNC_ALWAYS,
        //         .pass_op = SG_STENCILOP_KEEP,
        //     },
        //     .back = {
        //         // .compare = SG_COMPAREFUNC_NOT_EQUAL,
        //         // .pass_op = SG_STENCILOP_REPLACE,
        //         .compare = SG_COMPAREFUNC_ALWAYS,
        //         .pass_op = SG_STENCILOP_KEEP,
        //     },
        //     .ref = 1,
        //     .read_mask = 1,
        // },
        .sample_count = Constant::SampleCount,
        .colors = {
            [0] = {
                .blend = {
                    .enabled = true,
                    .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
                    .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                    .src_factor_alpha = SG_BLENDFACTOR_ONE,
                    .dst_factor_alpha = SG_BLENDFACTOR_ONE,
                },
            },
        },
        .depth = {
            .write_enabled = true,
            .compare = SG_COMPAREFUNC_LESS_EQUAL,  // FIXME: SG_COMPAREFUNC_LESS?
        },
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
    };
    pipeline_frontface = sg_make_pipeline(&pipeline_desc);

    pipeline_desc.cull_mode = SG_CULLMODE_NONE;
    pipeline_bothface = sg_make_pipeline(&pipeline_desc);
}

void Routine::selectNextMotion() {
    // Select next MMD motion by weighted rate.
    unsigned int rnd = randDist(rand);
    unsigned int sum = 0;
    const auto motionCount = motionWeights.size();
    motionID = motionCount - 1;
    for (size_t i = 0; i < motionCount; ++i) {
        sum += motionWeights[i];
        if (sum >= rnd) {
            motionID = i;
            break;
        }
    }
    if (motionID >= motionCount)
        Err::Exit("Internal error: unreachable:", __FILE__ ":", __LINE__, ':', __func__);
}

void Routine::Update() {
    const auto size{Context::getWindowSize()};
    const auto model = mmd.GetModel();
    const size_t vertCount = model->GetVertexCount();
    const double vmdFrame = stm_sec(stm_since(timeBeginAnimation)) * Constant::VmdFPS;
    const double elapsedTime = stm_sec(stm_since(timeLastFrame));

    if (mmd.GetCameraAnimation()) {
        auto& cameraAnimation = mmd.GetCameraAnimation();
        cameraAnimation->Evaluate(vmdFrame);
        const auto& mmdCamera = cameraAnimation->GetCamera();
        saba::MMDLookAtCamera lookAtCamera(mmdCamera);
        viewMatrix = glm::lookAt(lookAtCamera.m_eye, lookAtCamera.m_center, lookAtCamera.m_up);
        projectionMatrix = glm::perspectiveFovRH(
                mmdCamera.m_fov,
                static_cast<float>(size.x),
                static_cast<float>(size.y),
                1.0f,
                10000.0f);
    } else {
        viewMatrix = glm::lookAt(glm::vec3(0, 10, 50), glm::vec3(0, 10, 0), glm::vec3(0, 1, 0));
        projectionMatrix = glm::perspectiveFovRH(
                glm::radians(30.0f),
                static_cast<float>(size.x),
                static_cast<float>(size.y),
                1.0f,
                10000.0f);
    }

    model->BeginAnimation();
    if (needBridgeMotions) {
        mmd.GetAnimations()[motionID]->Evaluate(0.0f, stm_sec(stm_since(timeBeginAnimation)));
		model->UpdateMorphAnimation();
		model->UpdateNodeAnimation(false);
		model->UpdatePhysicsAnimation(elapsedTime);
		model->UpdateNodeAnimation(true);
        if (vmdFrame >= Constant::VmdFPS) {
            needBridgeMotions = false;
            timeBeginAnimation = stm_now();
        }
    } else {
        model->UpdateAllAnimation(mmd.GetAnimations()[motionID].get(), vmdFrame, elapsedTime);
    }
    model->EndAnimation();
    model->Update();

    sg_update_buffer(posVB, sg_range{
                .size = vertCount * sizeof(glm::vec3),
                .ptr = model->GetUpdatePositions(),
            });
    sg_update_buffer(normVB, sg_range{
                .size = vertCount * sizeof(glm::vec3),
                .ptr = model->GetUpdateNormals(),
            });
    sg_update_buffer(uvVB, sg_range{
                .size = vertCount * sizeof(glm::vec2),
                .ptr = model->GetUpdateUVs(),
            });

    timeLastFrame = stm_now();
    if (vmdFrame > mmd.GetAnimations()[motionID]->GetMaxKeyTime()) {
        model->SaveBaseAnimation();
        timeBeginAnimation = timeLastFrame;
        selectNextMotion();
        needBridgeMotions = true;
    }

    // TODO: cameraAnimation->reset() should be called anywhere?
}

void Routine::Draw() {
    const auto size{Context::getWindowSize()};
    const auto model = mmd.GetModel();
    // const auto& dxMat = glm::mat4(
    //     1.0f, 0.0f, 0.0f, 0.0f,
    //     0.0f, 1.0f, 0.0f, 0.0f,
    //     0.0f, 0.0f, 0.5f, 0.0f,
    //     0.0f, 0.0f, 0.5f, 1.0f
    // );

    auto userView = userViewport_.getMatrix();
    auto world = glm::mat4(1.0f);
    auto wv = userView * viewMatrix * world;
    auto wvp = userView * projectionMatrix * viewMatrix * world;
    // wvp = dxMat * wvp;
    auto wvit = glm::mat3(userView * viewMatrix * world);
    wvit = glm::inverse(wvit);
    wvit = glm::transpose(wvit);

    auto lightColor = glm::vec3(1, 1, 1);
    auto lightDir = glm::vec3(-0.5f, -1.0f, -0.5f);
    // auto viewMat = glm::mat3(viewMatrix);
    lightDir = glm::mat3(viewMatrix) * lightDir;

    sg_begin_default_pass(&passAction, size.x, size.y);

    const size_t subMeshCount = model->GetSubMeshCount();
    for (size_t i = 0; i < subMeshCount; ++i) {
        const auto& subMesh = model->GetSubMeshes()[i];
        // const auto& shader = shaderMMD;
        const auto & material = materials[subMesh.m_materialID];
        const auto& mmdMaterial = material.material;

        if (mmdMaterial.m_alpha == 0)
            continue;

        u_mmd_vs_t u_mmd_vs = {
            .u_WV = wv,
            .u_WVP = wvp,
        };

        u_mmd_fs_t u_mmd_fs = {
            .u_Alpha = mmdMaterial.m_alpha,
            .u_Ambient = mmdMaterial.m_ambient,
            .u_Diffuse = mmdMaterial.m_diffuse,
            .u_Specular = mmdMaterial.m_specular,
            .u_SpecularPower = mmdMaterial.m_specularPower,
            .u_TexMode = 0,
            .u_SphereTexMode = 0,
            .u_ToonTexMode = 0,
            .u_LightColor = lightColor,
            .u_LightDir = lightDir,
        };

#if 1
        if (material.texture) {
            binds.fs_images[SLOT_u_Tex_mmd] = *material.texture;
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
            binds.fs_images[SLOT_u_Tex_mmd] = dummyTex;
        }
#else
        binds.fs_images[SLOT_u_Tex_mmd] = dummyTex;
#endif

#if 1
        if (material.spTexture) {
            binds.fs_images[SLOT_u_SphereTex] = *material.spTexture;
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
            binds.fs_images[SLOT_u_SphereTex] = dummyTex;
        }
#else
        binds.fs_images[SLOT_u_SphereTex] = dummyTex;
#endif

#if 1
        if (material.toonTexture) {
            binds.fs_images[SLOT_u_ToonTex] = *material.toonTexture;
            u_mmd_fs.u_ToonTexMulFactor = mmdMaterial.m_toonTextureMulFactor;
            u_mmd_fs.u_ToonTexAddFactor = mmdMaterial.m_toonTextureAddFactor;
            u_mmd_fs.u_ToonTexMode = 1;
        } else {
            binds.fs_images[SLOT_u_ToonTex] = dummyTex;
        }
#else
        binds.fs_images[SLOT_u_ToonTex] = dummyTex;
#endif

        if (mmdMaterial.m_bothFace)
            sg_apply_pipeline(pipeline_bothface);
        else
            sg_apply_pipeline(pipeline_frontface);
        sg_apply_bindings(binds);
        // TODO: Transput glBindTexutre
        sg_apply_uniforms(SG_SHADERSTAGE_VS, SLOT_u_mmd_vs, SG_RANGE(u_mmd_vs));
        sg_apply_uniforms(SG_SHADERSTAGE_FS, SLOT_u_mmd_fs, SG_RANGE(u_mmd_fs));

        sg_draw(subMesh.m_beginIndex, subMesh.m_vertexCount, 1);
    }

    sg_end_pass();
    // transparentFBO.Draw();
    sg_commit();
}

void Routine::Terminate() {
    if (!shouldTerminate)
        return;

    motionID = 0;
    motionWeights.clear();
    induces.clear();
    texImages.clear();
    textures.clear();
    toonTextures.clear();
    materials.clear();

    sg_destroy_shader(shaderMMD);

    sg_destroy_buffer(posVB);
    sg_destroy_buffer(normVB);
    sg_destroy_buffer(uvVB);

    sg_destroy_image(dummyTex);

    sg_destroy_pipeline(pipeline_frontface);
    sg_destroy_pipeline(pipeline_bothface);

    sg_shutdown();

    shouldTerminate = false;
}

void Routine::OnMouseDown() {
    userViewport_.onMouseDown();
}

void Routine::OnMouseDragged() {
    userViewport_.onMouseDragged();
}

void Routine::OnWheelScrolled(float delta) {
    userViewport_.onWheelScrolled(delta);
}

std::optional<Routine::ImageMap::const_iterator> Routine::loadImage(std::string path) {
    auto itr = texImages.find(path);
    if (itr == texImages.cend()) {
        Image img;
        if (img.loadFromFile(path)) {
            texImages.emplace(path, std::move(img));
            return texImages.find(path);
        } else {
            Err::Log("Failed to load image:", path);
            return std::nullopt;
        }
    } else {
        return itr;
    }
}

std::optional<sg_image> Routine::getTexture(std::string path) {
    const auto itr = loadImage(path);
    if (!itr)
        return std::nullopt;

    const auto& image = (*itr)->second;
    return sg_make_image(sg_image_desc{
        .type = SG_IMAGETYPE_2D,
        .render_target = false,
        .width = static_cast<int>(image.width),
        .height = static_cast<int>(image.height),
        .usage = SG_USAGE_IMMUTABLE,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
        .data.subimage[0][0] = {
            .size = image.pixels.size(),
            .ptr = image.pixels.data(),
        },
    });
}

std::optional<sg_image> Routine::getToonTexture(std::string path) {
    const auto itr = loadImage(path);
    if (!itr)
        return std::nullopt;

    const auto& image = (*itr)->second;
    return sg_make_image(sg_image_desc{
        .type = SG_IMAGETYPE_2D,
        .render_target = false,
        .width = static_cast<int>(image.width),
        .height = static_cast<int>(image.height),
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
        .usage = SG_USAGE_IMMUTABLE,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
        .data.subimage[0][0] = {
            .size = image.pixels.size(),
            .ptr = image.pixels.data(),
        },
    });
}
