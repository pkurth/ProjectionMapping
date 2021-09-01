#ifndef PROJECTOR_HLSLI
#define PROJECTOR_HLSLI


#define PROJECTOR_SOLVER_BLOCK_SIZE 16


struct projector_vp
{
    mat4 viewProj;
    mat4 invViewProj;
};

struct projector_solver_cb
{
    uint32 currentIndex;
    uint32 numProjectors;
    uint32 width;
    uint32 height;
};


#define PROJECTOR_SOLVER_RS \
    "RootFlags(0), " \
    "RootConstants(num32BitConstants=4, b0),"  \
    "SRV(t0, space=0), " \
    "DescriptorTable( SRV(t0, space=1, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "DescriptorTable( SRV(t0, space=2, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "DescriptorTable( SRV(t0, space=3, numDescriptors=unbounded, flags=DESCRIPTORS_VOLATILE) ), " \
    "DescriptorTable( UAV(u0, space=0, numDescriptors=1) ), " \
    "StaticSampler(s0," \
            "addressU = TEXTURE_ADDRESS_BORDER," \
            "addressV = TEXTURE_ADDRESS_BORDER," \
            "addressW = TEXTURE_ADDRESS_BORDER," \
            "filter = FILTER_MIN_MAG_MIP_LINEAR," \
            "borderColor = STATIC_BORDER_COLOR_OPAQUE_BLACK)"

#define PROJECTOR_SOLVER_RS_CB                  0
#define PROJECTOR_SOLVER_RS_VIEWPROJS           1
#define PROJECTOR_SOLVER_RS_RENDER_RESULTS      2
#define PROJECTOR_SOLVER_RS_DEPTH_TEXTURES      3
#define PROJECTOR_SOLVER_RS_INTENSITIES         4
#define PROJECTOR_SOLVER_RS_CURRENT_INTENSITY   5

#endif
