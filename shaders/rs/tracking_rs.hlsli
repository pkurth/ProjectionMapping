#ifndef TRACKING_RS_HLSLI
#define TRACKING_RS_HLSLI

#include "camera.hlsli"

struct visualize_depth_cb
{
	mat4 vp;
	mat4 colorCameraV;
	intrinsics_cb colorCameraIntrinsics;
	distortion_cb colorCameraDistortion;
	float depthScale;
	uint32 depthWidth;
	uint32 colorWidth;
	uint32 colorHeight;
};

struct create_correspondences_cb
{
	mat4 m;
	intrinsics_cb intrinsics;
	distortion_cb distortion;
	uint32 width;
	uint32 height;
	float depthScale;
};


#define VISUALIZE_DEPTH_RS \
	"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |" \
	"DENY_HULL_SHADER_ROOT_ACCESS |" \
	"DENY_DOMAIN_SHADER_ROOT_ACCESS |" \
	"DENY_GEOMETRY_SHADER_ROOT_ACCESS)," \
	"RootConstants(num32BitConstants=50, b0, visibility=SHADER_VISIBILITY_VERTEX), " \
	"DescriptorTable(SRV(t0, numDescriptors=2), visibility=SHADER_VISIBILITY_VERTEX), " \
	"DescriptorTable(SRV(t0, numDescriptors=1, space=1), visibility=SHADER_VISIBILITY_PIXEL), " \
	"StaticSampler(s0," \
            "addressU = TEXTURE_ADDRESS_BORDER," \
            "addressV = TEXTURE_ADDRESS_BORDER," \
            "addressW = TEXTURE_ADDRESS_BORDER," \
            "filter = FILTER_MIN_MAG_MIP_LINEAR," \
            "borderColor = STATIC_BORDER_COLOR_OPAQUE_BLACK)"

#define VISUALIZE_DEPTH_RS_CB						0
#define VISUALIZE_DEPTH_RS_DEPTH_TEXTURE_AND_TABLE	1
#define VISUALIZE_DEPTH_RS_COLOR_TEXTURE			2



#define CREATE_CORRESPONDENCES_RS \
	"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |" \
	"DENY_HULL_SHADER_ROOT_ACCESS |" \
	"DENY_DOMAIN_SHADER_ROOT_ACCESS |" \
	"DENY_GEOMETRY_SHADER_ROOT_ACCESS)," \
	"RootConstants(num32BitConstants=32, b0, visibility=SHADER_VISIBILITY_ALL), " \
	"DescriptorTable(SRV(t0, numDescriptors=2), visibility=SHADER_VISIBILITY_PIXEL), " \

#define CREATE_CORRESPONDENCES_RS_CB						0
#define CREATE_CORRESPONDENCES_RS_DEPTH_TEXTURE_AND_TABLE	1


#endif
