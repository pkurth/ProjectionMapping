#ifndef PROJECTOR_HLSLI
#define PROJECTOR_HLSLI


#define PROJECTOR_BLOCK_SIZE 16


struct projector_cb
{
    mat4 viewProj;
    mat4 invViewProj;
    vec4 position;
    vec2 screenDims;
    vec2 invScreenDims;
    vec4 projectionParams;
    vec4 forward;
};



static float getAngleAttenuation(vec3 N, vec3 V)
{
    return saturate(dot(N, V));
}

static float getDistanceAttenuation(float distance, float referenceDistance)
{
    return 1.f / exp2(distance - referenceDistance);
}




struct projector_attenuation_cb
{
    uint32 index;
    float referenceDistance;
    float desiredWhiteValue;
};

#define PROJECTOR_ATTENUATION_RS \
    "RootFlags(0), " \
    "RootConstants(num32BitConstants=3, b0),"  \
    "SRV(t0, space=0), " \
    "DescriptorTable( SRV(t0, space=1, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "DescriptorTable( SRV(t0, space=2, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "DescriptorTable( SRV(t0, space=3, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "DescriptorTable( UAV(u0, space=0, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "StaticSampler(s0," \
            "addressU = TEXTURE_ADDRESS_CLAMP," \
            "addressV = TEXTURE_ADDRESS_CLAMP," \
            "addressW = TEXTURE_ADDRESS_CLAMP," \
            "filter = FILTER_MIN_MAG_MIP_LINEAR)"


#define PROJECTOR_ATTENUATION_RS_CB                  0
#define PROJECTOR_ATTENUATION_RS_PROJECTORS          1
#define PROJECTOR_ATTENUATION_RS_RENDER_RESULTS      2
#define PROJECTOR_ATTENUATION_RS_WORLD_NORMALS       3
#define PROJECTOR_ATTENUATION_RS_DEPTH_TEXTURES      4
#define PROJECTOR_ATTENUATION_RS_OUTPUT              5



struct projector_mask_cb
{
    uint32 index;
    float colorMaskStrength;
};

#define PROJECTOR_MASK_RS \
    "RootFlags(0), " \
    "RootConstants(num32BitConstants=2, b0),"  \
    "SRV(t0, space=0), " \
    "DescriptorTable( SRV(t0, space=1, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "DescriptorTable( SRV(t0, space=2, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "DescriptorTable( SRV(t0, space=3, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "DescriptorTable( SRV(t0, space=4, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "DescriptorTable( UAV(u0, space=0, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "StaticSampler(s0," \
            "addressU = TEXTURE_ADDRESS_CLAMP," \
            "addressV = TEXTURE_ADDRESS_CLAMP," \
            "addressW = TEXTURE_ADDRESS_CLAMP," \
            "filter = FILTER_MIN_MAG_MIP_LINEAR)"


#define PROJECTOR_MASK_RS_CB                  0
#define PROJECTOR_MASK_RS_PROJECTORS          1
#define PROJECTOR_MASK_RS_DEPTH_TEXTURES      2
#define PROJECTOR_MASK_RS_DEPTH_MASKS         3
#define PROJECTOR_MASK_RS_COLOR_MASKS         4
#define PROJECTOR_MASK_RS_BEST_MASKS          5
#define PROJECTOR_MASK_RS_OUTPUT              6




struct projector_intensity_cb
{
    uint32 index;
    uint32 numProjectors;
};

#define PROJECTOR_INTENSITIES_RS \
    "RootFlags(0), " \
    "RootConstants(num32BitConstants=2, b0),"  \
    "SRV(t0, space=0), " \
    "DescriptorTable( SRV(t0, space=1, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "DescriptorTable( SRV(t0, space=2, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "DescriptorTable( SRV(t0, space=3, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "DescriptorTable( UAV(u0, space=0, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "StaticSampler(s0," \
        "addressU = TEXTURE_ADDRESS_BORDER," \
        "addressV = TEXTURE_ADDRESS_BORDER," \
        "addressW = TEXTURE_ADDRESS_BORDER," \
        "filter = FILTER_MIN_MAG_MIP_LINEAR," \
        "borderColor = STATIC_BORDER_COLOR_OPAQUE_BLACK), " \
    "StaticSampler(s1," \
        "addressU = TEXTURE_ADDRESS_BORDER," \
        "addressV = TEXTURE_ADDRESS_BORDER," \
        "addressW = TEXTURE_ADDRESS_BORDER," \
        "filter = FILTER_MIN_MAG_MIP_POINT," \
        "borderColor = STATIC_BORDER_COLOR_OPAQUE_WHITE)"

#define PROJECTOR_INTENSITIES_RS_CB                 0
#define PROJECTOR_INTENSITIES_RS_PROJECTORS         1
#define PROJECTOR_INTENSITIES_RS_ATTENUATIONS       2
#define PROJECTOR_INTENSITIES_RS_MASKS              3
#define PROJECTOR_INTENSITIES_RS_DEPTH_TEXTURES     4
#define PROJECTOR_INTENSITIES_RS_OUT_INTENSITIES    5



struct projector_best_mask_cb
{
    uint32 index;
    uint32 numProjectors;
    vec2 screenDims;
};

#define PROJECTOR_BEST_MASK_RS \
    "RootFlags(0), " \
    "RootConstants(num32BitConstants=4, b0),"  \
    "SRV(t0, space=0), " \
    "DescriptorTable( SRV(t0, space=1, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "DescriptorTable( SRV(t0, space=2, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "DescriptorTable( UAV(u0, space=0, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "StaticSampler(s0," \
        "addressU = TEXTURE_ADDRESS_BORDER," \
        "addressV = TEXTURE_ADDRESS_BORDER," \
        "addressW = TEXTURE_ADDRESS_BORDER," \
        "filter = FILTER_MIN_MAG_MIP_LINEAR," \
        "borderColor = STATIC_BORDER_COLOR_OPAQUE_BLACK), " \
    "StaticSampler(s1," \
        "addressU = TEXTURE_ADDRESS_BORDER," \
        "addressV = TEXTURE_ADDRESS_BORDER," \
        "addressW = TEXTURE_ADDRESS_BORDER," \
        "filter = FILTER_MIN_MAG_MIP_POINT," \
        "borderColor = STATIC_BORDER_COLOR_OPAQUE_WHITE)"

#define PROJECTOR_BEST_MASK_RS_CB                   0
#define PROJECTOR_BEST_MASK_RS_PROJECTORS           1
#define PROJECTOR_BEST_MASK_RS_ATTENUATIONS         2
#define PROJECTOR_BEST_MASK_RS_DEPTH_TEXTURES       3
#define PROJECTOR_BEST_MASK_RS_OUT_MASK             4
















struct projector_visualization_cb
{
    uint32 numProjectors;
    float referenceDistance;
};


#define PROJECTOR_SIMULATION_RS \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |" \
    "DENY_HULL_SHADER_ROOT_ACCESS |" \
    "DENY_DOMAIN_SHADER_ROOT_ACCESS |" \
    "DENY_GEOMETRY_SHADER_ROOT_ACCESS)," \
    "RootConstants(num32BitConstants=32, b0, visibility=SHADER_VISIBILITY_VERTEX), " \
    "RootConstants(num32BitConstants=2, b0, space=1, visibility=SHADER_VISIBILITY_PIXEL),"  \
    "SRV(t0, space=0), " \
    "DescriptorTable( SRV(t0, space=1, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "DescriptorTable( SRV(t0, space=2, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "StaticSampler(s0," \
            "addressU = TEXTURE_ADDRESS_BORDER," \
            "addressV = TEXTURE_ADDRESS_BORDER," \
            "addressW = TEXTURE_ADDRESS_BORDER," \
            "filter = FILTER_MIN_MAG_MIP_LINEAR," \
            "borderColor = STATIC_BORDER_COLOR_OPAQUE_BLACK)"


#define PROJECTOR_SIMULATION_RS_TRANSFORM           0
#define PROJECTOR_SIMULATION_RS_CB                  1
#define PROJECTOR_SIMULATION_RS_VIEWPROJS           2
#define PROJECTOR_SIMULATION_RS_RENDER_RESULTS      3
#define PROJECTOR_SIMULATION_RS_DEPTH_TEXTURES      4











#define present_sdr 0
#define present_hdr 1


struct present_cb
{
    uint32 displayMode;
    float standardNits;
    float sharpenStrength;
    uint32 offset; // x-offset | y-offset.
};

#define PROJECTOR_PRESENT_RS \
    "RootFlags(0), " \
    "RootConstants(num32BitConstants=4, b0),"  \
    "DescriptorTable( UAV(u0, numDescriptors = 1), SRV(t0, numDescriptors = 2) )"

#define PROJECTOR_PRESENT_RS_CB               0
#define PROJECTOR_PRESENT_RS_TEXTURES         1




#define PROJECTOR_INTENSITY_VISUALIZATION_RS \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |" \
    "DENY_HULL_SHADER_ROOT_ACCESS |" \
    "DENY_DOMAIN_SHADER_ROOT_ACCESS |" \
    "DENY_GEOMETRY_SHADER_ROOT_ACCESS)," \
    "RootConstants(num32BitConstants=32, b0, visibility=SHADER_VISIBILITY_VERTEX), " \
    "RootConstants(num32BitConstants=2, b0, space=1, visibility=SHADER_VISIBILITY_PIXEL),"  \
    "SRV(t0, space=0, visibility=SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable( SRV(t0, space=1, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL ), " \
    "DescriptorTable( SRV(t0, space=2, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL ), " \
    "StaticSampler(s0," \
            "addressU = TEXTURE_ADDRESS_BORDER," \
            "addressV = TEXTURE_ADDRESS_BORDER," \
            "addressW = TEXTURE_ADDRESS_BORDER," \
            "filter = FILTER_MIN_MAG_MIP_LINEAR," \
            "borderColor = STATIC_BORDER_COLOR_OPAQUE_BLACK," \
            "visibility = SHADER_VISIBILITY_PIXEL)"


#define PROJECTOR_INTENSITY_VISUALIZATION_RS_TRANSFORM           0
#define PROJECTOR_INTENSITY_VISUALIZATION_RS_CB                  1
#define PROJECTOR_INTENSITY_VISUALIZATION_RS_VIEWPROJS           2
#define PROJECTOR_INTENSITY_VISUALIZATION_RS_DEPTH_TEXTURES      3
#define PROJECTOR_INTENSITY_VISUALIZATION_RS_INTENSITIES         4



#endif
