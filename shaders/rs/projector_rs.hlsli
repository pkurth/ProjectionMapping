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
    vec4 padding[2];
};

struct projector_solver_cb
{
    uint32 currentIndex;
    uint32 numProjectors;
};


#define PROJECTOR_SOLVER_RS \
    "RootFlags(0), " \
    "RootConstants(num32BitConstants=2, b0),"  \
    "SRV(t0, space=0), " \
    "DescriptorTable( SRV(t0, space=1, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "DescriptorTable( SRV(t0, space=2, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "DescriptorTable( SRV(t0, space=3, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "DescriptorTable( SRV(t0, space=4, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "DescriptorTable( UAV(u0, space=0, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "StaticSampler(s0," \
            "addressU = TEXTURE_ADDRESS_BORDER," \
            "addressV = TEXTURE_ADDRESS_BORDER," \
            "addressW = TEXTURE_ADDRESS_BORDER," \
            "filter = FILTER_MIN_MAG_MIP_LINEAR," \
            "borderColor = STATIC_BORDER_COLOR_OPAQUE_BLACK)"

#define PROJECTOR_SOLVER_RS_CB                  0
#define PROJECTOR_SOLVER_RS_VIEWPROJS           1
#define PROJECTOR_SOLVER_RS_RENDER_RESULTS      2
#define PROJECTOR_SOLVER_RS_WORLD_NORMALS       3
#define PROJECTOR_SOLVER_RS_DEPTH_TEXTURES      4
#define PROJECTOR_SOLVER_RS_INTENSITIES         5
#define PROJECTOR_SOLVER_RS_OUT_INTENSITIES     6








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






#endif
