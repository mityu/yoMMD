// vim: set commentstring=//\ %s:
// Ported from these, which distributed under MIT License.
//  - https://github.com/benikabocha/saba/blob/fa2ea89b1e2108271591c8bcdfcff8fb32e7f528/viewer/Saba/Viewer/resource/shader/mmd.vert
//  - https://github.com/benikabocha/saba/blob/fa2ea89b1e2108271591c8bcdfcff8fb32e7f528/viewer/Saba/Viewer/resource/shader/mmd.frag
//  - (License) https://github.com/benikabocha/saba/blob/fa2ea89b1e2108271591c8bcdfcff8fb32e7f528/LICENCE
#version 300

@ctype mat4 glm::mat4
@ctype vec2 glm::vec2
@ctype vec3 glm::vec3
@ctype vec4 glm::vec4

@vs mmd_vs
in vec3 in_Pos;
in vec3 in_Nor;
in vec2 in_UV;

out vec3 vs_Pos;
out vec3 vs_Nor;
out vec2 vs_UV;

layout(binding=0) uniform u_mmd_vs {
    mat4 u_WV;
    mat4 u_WVP;
};

void main()
{
    gl_Position = u_WVP * vec4(in_Pos, 1.0);

    vs_Pos = (u_WV * vec4(in_Pos, 1.0)).xyz;
    vs_Nor = mat3(u_WV) * in_Nor;
    vs_UV = in_UV;
    // vs_UV = vec2(in_UV.x, 1.0 - in_UV.y);
}
@end

@fs mmd_fs
in vec3 vs_Pos;
in vec3 vs_Nor;
in vec2 vs_UV;

out vec4 out_Color;

layout(binding=1) uniform u_mmd_fs {
    float u_Alpha;
    vec3 u_Diffuse;
    vec3 u_Ambient;
    vec3 u_Specular;
    float u_SpecularPower;
    vec3 u_LightColor;
    vec3 u_LightDir;

    int u_TexMode;
    vec4 u_TexMulFactor;
    vec4 u_TexAddFactor;

    int u_ToonTexMode;
    vec4 u_ToonTexMulFactor;
    vec4 u_ToonTexAddFactor;

    int u_SphereTexMode;
    vec4 u_SphereTexMulFactor;
    vec4 u_SphereTexAddFactor;
};

layout(binding=0) uniform texture2D u_Tex;
layout(binding=1) uniform texture2D u_ToonTex;
layout(binding=2) uniform texture2D u_SphereTex;
layout(binding=0) uniform sampler u_Tex_smp;
layout(binding=1) uniform sampler u_ToonTex_smp;
layout(binding=2) uniform sampler u_SphereTex_smp;

vec3 ComputeTexMulFactor(vec3 texColor, vec4 factor)
{
    vec3 ret = texColor * factor.rgb;
    return mix(vec3(1.0, 1.0, 1.0), ret, factor.a);
}

vec3 ComputeTexAddFactor(vec3 texColor, vec4 factor)
{
    vec3 ret = texColor + (texColor - vec3(1.0)) * factor.a ;
    ret = clamp(ret, vec3(0.0), vec3(1.0))+ factor.rgb;
    return ret;
}

void main()
{
    vec3 eyeDir = normalize(vs_Pos);
    vec3 lightDir = normalize(-u_LightDir);
    vec3 nor = normalize(vs_Nor);
    float ln = dot(nor, lightDir);
    ln = clamp(ln + 0.5, 0.0, 1.0);
    vec3 color = vec3(0.0, 0.0, 0.0);
    float alpha = u_Alpha;
    vec3 diffuseColor = u_Diffuse * u_LightColor;
    color = diffuseColor;
    color += u_Ambient;
    color = clamp(color, 0.0, 1.0);

    if (u_TexMode != 0)
    {
        vec4 texColor = texture(sampler2D(u_Tex, u_Tex_smp), vs_UV);
        texColor.rgb = ComputeTexMulFactor(texColor.rgb, u_TexMulFactor);
        texColor.rgb = ComputeTexAddFactor(texColor.rgb, u_TexAddFactor);
        color *= texColor.rgb;
        if (u_TexMode == 2)
        {
            alpha *= texColor.a;
        }
    }

    if (alpha == 0.0)
    {
        discard;
    }

    if (u_SphereTexMode != 0)
    {
        vec2 spUV = vec2(0.0);
        spUV.x = nor.x * 0.5 + 0.5;
        spUV.y = 1.0 - (nor.y * 0.5 + 0.5);
        vec3 spColor = texture(sampler2D(u_SphereTex, u_SphereTex_smp), spUV).rgb;
        spColor = ComputeTexMulFactor(spColor, u_SphereTexMulFactor);
        spColor = ComputeTexAddFactor(spColor, u_SphereTexAddFactor);
        if (u_SphereTexMode == 1)
        {
            color *= spColor;
        }
        else if (u_SphereTexMode == 2)
        {
            color += spColor;
        }
    }

    if (u_ToonTexMode != 0)
    {
        // vec3 toonColor = texture(sampler2D(u_ToonTex, u_ToonTex_smp), vec2(0.0, 1.0 - ln)).rgb;
        vec3 toonColor = texture(sampler2D(u_ToonTex, u_ToonTex_smp), vec2(0.0, ln)).rgb;
        toonColor = ComputeTexMulFactor(toonColor, u_ToonTexMulFactor);
        toonColor = ComputeTexAddFactor(toonColor, u_ToonTexAddFactor);
        color *= toonColor;
    }

    vec3 specular = vec3(0.0);
    if (u_SpecularPower > 0)
    {
        vec3 halfVec = normalize(eyeDir + lightDir);
        vec3 specularColor = u_Specular * u_LightColor;
        specular += pow(max(0.0, dot(halfVec, nor)), u_SpecularPower) * specularColor;
    }

    color += specular;

    out_Color = vec4(color, alpha);
}
@end

@program mmd mmd_vs mmd_fs
