#include <functional>
#include <optional>
#include <memory>
#include <string>
#include <string_view>
#include "sokol_gfx.h"
#include "sokol_log.h"
#include "Saba/Model/MMD/MMDMaterial.h"
#include "Saba/Model/MMD/MMDModel.h"
#include "Saba/Base/Path.h"
#include "Saba/Model/MMD/PMDModel.h"
#include "Saba/Model/MMD/PMXModel.h"
#include "Saba/Model/MMD/VMDFile.h"
#include "Saba/Model/MMD/VMDAnimation.h"
#include "Saba/Model/MMD/VMDCameraAnimation.h"
#include "yommd.hpp"
#include "yommd.glsl.h"


Material::Material(const saba::MMDMaterial& mat) :
    material(mat),
    textureHasAlpha(false)
{}

void MMD::Load() {
    const std::string modelPath = "./misc/model/miku/miku.pmx";
    const std::string motionPath = "./misc/motion/nobi.vmd";
    const std::string resourcePath = "./misc/resource/mmd/";

    auto ext = saba::PathUtil::GetExt(modelPath);
    if (ext == "pmx") {
        auto pmx = std::make_unique<saba::PMXModel>();
        if (!pmx->Load(modelPath, resourcePath)) {
            Err::Exit("Failed to load PMX:", modelPath);
        }
        model = std::move(pmx);
    } else if (ext == "pmd") {
        auto pmd = std::make_unique<saba::PMDModel>();
        if (!pmd->Load(modelPath, resourcePath)) {
            Err::Exit("Failed to load PMD:", modelPath);
        }
        model = std::move(pmd);
    } else {
        Err::Exit("Unsupported MMD file:", modelPath);
    }

    model->InitializeAnimation();

    auto vmdAnim = std::make_unique<saba::VMDAnimation>();
    if (!vmdAnim->Create(model)) {
        Err::Exit("Failed to create VMDAnimation.");
    }
    saba::VMDFile vmdFile;
    if (!saba::ReadVMDFile(&vmdFile, motionPath.c_str())) {
        Err::Exit("Failed to read VMD file.");
    }
    if (!vmdAnim->Add(vmdFile)) {
        Err::Exit("Failed to add VMDAnimation");
    }
    // TODO: Read camera.

    vmdAnim->SyncPhysics(0.0f);
    animation = std::move(vmdAnim);
}

bool MMD::IsLoaded() const {
    return static_cast<bool>(model);
}

const std::shared_ptr<saba::MMDModel> MMD::GetModel() const {
    return model;
}

const std::unique_ptr<saba::VMDAnimation>& MMD::GetAnimation() const {
    return animation;
}

Routine::Routine() :
    passAction({.colors[0] = {.action = SG_ACTION_CLEAR, .value = {.a = 0}}})
{}

Routine::~Routine() {
    Terminate();
}

void Routine::LoadMMD() {
    if (mmd.IsLoaded()) {
        Err::Exit("Model is already loaded.");
    }
    mmd.Load();
}

void Routine::Init() {
    if (!mmd.IsLoaded()) {
        Err::Exit("Internal Error:", "function", __func__,
                "must be called after loading MMD model.");
    }

    sg_desc desc = {
        .context = Context::getSokolContext(),
        .logger.func = slog_func,
    };
    sg_setup(&desc);

    const sg_backend backend = sg_query_backend();
    shaderMMD = sg_make_shader(mmd_shader_desc(backend));

    initBuffers();
    initTextures();
    initPipeline();

    binds = (sg_bindings){
        .vertex_buffers[ATTR_mmd_vs_in_Pos] = posVB,
        .vertex_buffers[ATTR_mmd_vs_in_Nor] = normVB,
        .vertex_buffers[ATTR_mmd_vs_in_UV] = uvVB,
        .index_buffer = ibo,
    };

    shouldTerminate = true;
}

void Routine::initBuffers() {
    static std::vector<uint32_t> induces;
    const auto model = mmd.GetModel();
    const size_t vertCount = model->GetVertexCount();
    const size_t indexSize = model->GetIndexElementSize();

    posVB = sg_make_buffer((sg_buffer_desc){
                .type = SG_BUFFERTYPE_VERTEXBUFFER,
                .usage = SG_USAGE_DYNAMIC,
                .size = vertCount * sizeof(glm::vec3),
            });
    normVB = sg_make_buffer((sg_buffer_desc){
                .type = SG_BUFFERTYPE_VERTEXBUFFER,
                .usage = SG_USAGE_DYNAMIC,
                .size = vertCount * sizeof(glm::vec3),
            });
    uvVB = sg_make_buffer((sg_buffer_desc){
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

    ibo = sg_make_buffer((sg_buffer_desc){
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

    dummyTex = sg_make_image((sg_image_desc){
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
    sg_pipeline_desc pipeline_desc ={
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
        .sample_count = SAMPLE_COUNT,
        .colors = {
            [0] = {
                .blend = {
                    .enabled = true,
                    .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
                    .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                    .src_factor_alpha = SG_BLENDFACTOR_SRC_ALPHA,
                    .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                },
            },
        },
        .depth = {
            .write_enabled = true,
            .compare = SG_COMPAREFUNC_LESS,
        },
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
    };
    pipeline = sg_make_pipeline(&pipeline_desc);

    pipeline_desc.cull_mode = SG_CULLMODE_NONE;
    pipeline_bothface = sg_make_pipeline(&pipeline_desc);
}

void Routine::Update() {
    static float animTime = 0.0f;
    const auto model = mmd.GetModel();
    const size_t vertCount = model->GetVertexCount();

    animTime += 0.5f;
    model->BeginAnimation();
    model->UpdateAllAnimation(mmd.GetAnimation().get(), animTime, 1.0f / 60.0f);
    model->EndAnimation();
    model->Update();

    // static std::vector<glm::vec3> verts, norms;
    // static std::vector<glm::vec2> uvs;
    // verts.resize(vertCount);
    // norms.resize(vertCount);
    // uvs.resize(vertCount);
    // const auto origVerts = model->GetUpdatePositions();
    // const auto origNorms = model->GetUpdateNormals();
    // const auto origUVs = model->GetUpdateUVs();
    // for (size_t i = 0; i < vertCount; ++i) {
    //     verts[i] = origVerts[i] * glm::vec3(1, 1, -1);
    //     norms[i] = origNorms[i] * glm::vec3(1, 1, -1);
    //     uvs[i] = glm::vec2(origUVs[i].x, 1.0f - origUVs[i].y);
    // }

    sg_update_buffer(posVB, (sg_range){
                .size = vertCount * sizeof(glm::vec3),
                .ptr = model->GetUpdatePositions(),
            });
    sg_update_buffer(normVB, (sg_range){
                .size = vertCount * sizeof(glm::vec3),
                .ptr = model->GetUpdateNormals(),
            });
    sg_update_buffer(uvVB, (sg_range){
                .size = vertCount * sizeof(glm::vec2),
                .ptr = model->GetUpdateUVs(),
            });

    // sg_update_buffer(posVB, (sg_range){
    //             .size = vertCount * sizeof(glm::vec3),
    //             .ptr = verts.data(),
    //         });
    // sg_update_buffer(normVB, (sg_range){
    //             .size = vertCount * sizeof(glm::vec3),
    //             .ptr = norms.data(),
    //         });
    // sg_update_buffer(uvVB, (sg_range){
    //             .size = vertCount * sizeof(glm::vec2),
    //             .ptr = uvs.data(),
    //         });
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

    // TODO: This should be done in Routine::Update().  Refer to sample mmd
    // viewer for when this should be inserted.
    viewMatrix = glm::lookAt(glm::vec3(0, 10, 50), glm::vec3(0, 10, 0), glm::vec3(0, 1, 0));
    projectionMatrix = glm::perspectiveFovRH(glm::radians(30.0f), float(size.first), float(size.second), 1.0f, 10000.0f);

    auto world = glm::mat4(1.0f);
    auto wv = viewMatrix * world;
    // auto wvp = dxMat * projectionMatrix * viewMatrix * world;
    auto wvp = projectionMatrix * viewMatrix * world;
    auto wvit = glm::mat3(viewMatrix * world);
    wvit = glm::inverse(wvit);
    wvit = glm::transpose(wvit);

    auto lightColor = glm::vec3(1, 1, 1);
    auto lightDir = glm::vec3(-0.5f, -1.0f, -0.5f);
    // auto viewMat = glm::mat3(viewMatrix);
    lightDir = glm::mat3(viewMatrix) * lightDir;

    sg_begin_default_pass(&passAction, size.first, size.second);
    sg_apply_viewport(0, 0, size.first, size.second, true);

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
            sg_apply_pipeline(pipeline);
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

    sg_destroy_shader(shaderMMD);

    sg_destroy_buffer(posVB);
    sg_destroy_buffer(normVB);
    sg_destroy_buffer(uvVB);

    sg_destroy_image(dummyTex);

    sg_destroy_pipeline(pipeline);

    sg_shutdown();

    shouldTerminate = false;
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
