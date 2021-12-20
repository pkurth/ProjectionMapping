#ifndef DEFAULT_PBR_RS_HLSLI
#define DEFAULT_PBR_RS_HLSLI

#include "transform.hlsli"

struct lighting_cb
{
    vec2 shadowMapTexelSize;
    float environmentIntensity;
};

#define DEFAULT_PBR_RS \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |" \
    "DENY_HULL_SHADER_ROOT_ACCESS |" \
    "DENY_DOMAIN_SHADER_ROOT_ACCESS |" \
    "DENY_GEOMETRY_SHADER_ROOT_ACCESS)," \
    "RootConstants(num32BitConstants=32, b0, visibility=SHADER_VISIBILITY_VERTEX),"  \
    "RootConstants(num32BitConstants=6, b0, space=1, visibility=SHADER_VISIBILITY_PIXEL),"  \
    "RootConstants(num32BitConstants=3, b2, space=1, visibility=SHADER_VISIBILITY_PIXEL),"  \
    "CBV(b1, space=1, visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t0, numDescriptors=4, space=1), visibility=SHADER_VISIBILITY_PIXEL), " \
    "CBV(b0, space=2, visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t0, space=2, numDescriptors=14), visibility=SHADER_VISIBILITY_PIXEL), " \
    "StaticSampler(s0," \
        "addressU = TEXTURE_ADDRESS_WRAP," \
        "addressV = TEXTURE_ADDRESS_WRAP," \
        "addressW = TEXTURE_ADDRESS_WRAP," \
        "filter = FILTER_ANISOTROPIC," \
        "visibility=SHADER_VISIBILITY_PIXEL)," \
    "StaticSampler(s1," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "filter = FILTER_ANISOTROPIC," \
        "visibility=SHADER_VISIBILITY_PIXEL)," \
    "StaticSampler(s2," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "filter = FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT," \
        "visibility=SHADER_VISIBILITY_PIXEL)"

#define DEFAULT_PBR_RS_MVP	                0
#define DEFAULT_PBR_RS_MATERIAL             1
#define DEFAULT_PBR_RS_LIGHTING             2
#define DEFAULT_PBR_RS_CAMERA               3
#define DEFAULT_PBR_RS_PBR_TEXTURES         4
#define DEFAULT_PBR_RS_SUN                  5
#define DEFAULT_PBR_RS_FRAME_CONSTANTS      6


#endif

