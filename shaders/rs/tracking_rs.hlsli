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

struct create_correspondences_vs_cb
{
	mat4 m;
	intrinsics_cb intrinsics;
	distortion_cb distortion;
	uint32 width;
	uint32 height;
};

#define tracking_direction_camera_to_render 0
#define tracking_direction_render_to_camera 1

struct create_correspondences_ps_cb
{
	float depthScale;
	float squaredPositionThreshold;
	float cosAngleThreshold;
	uint32 trackingDirection;
};

struct tracking_correspondence
{
	vec4 grad0;
	vec4 grad1;
};


#define VISUALIZE_DEPTH_RS \
	"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |" \
	"DENY_HULL_SHADER_ROOT_ACCESS |" \
	"DENY_DOMAIN_SHADER_ROOT_ACCESS |" \
	"DENY_GEOMETRY_SHADER_ROOT_ACCESS)," \
	"RootConstants(num32BitConstants=50, b0, visibility=SHADER_VISIBILITY_VERTEX), " \
	"DescriptorTable(SRV(t0, numDescriptors=3), visibility=SHADER_VISIBILITY_VERTEX), " \

#define VISUALIZE_DEPTH_RS_CB			0
#define VISUALIZE_DEPTH_RS_TEXTURES		1



#define CREATE_CORRESPONDENCES_DEPTH_ONLY_RS \
	"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |" \
	"DENY_HULL_SHADER_ROOT_ACCESS |" \
	"DENY_DOMAIN_SHADER_ROOT_ACCESS |" \
	"DENY_GEOMETRY_SHADER_ROOT_ACCESS |" \
	"DENY_PIXEL_SHADER_ROOT_ACCESS)," \
	"RootConstants(num32BitConstants=32, b0, visibility=SHADER_VISIBILITY_ALL)"

#define CREATE_CORRESPONDENCES_RS \
	"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |" \
	"DENY_HULL_SHADER_ROOT_ACCESS |" \
	"DENY_DOMAIN_SHADER_ROOT_ACCESS |" \
	"DENY_GEOMETRY_SHADER_ROOT_ACCESS)," \
	"RootConstants(num32BitConstants=32, b0, visibility=SHADER_VISIBILITY_ALL), " \
	"RootConstants(num32BitConstants=4, b1, visibility=SHADER_VISIBILITY_ALL), " \
	"DescriptorTable(SRV(t0, numDescriptors=2), UAV(u0, numDescriptors=2), visibility=SHADER_VISIBILITY_PIXEL)"

#define CREATE_CORRESPONDENCES_RS_VS_CB		0
#define CREATE_CORRESPONDENCES_RS_PS_CB		1
#define CREATE_CORRESPONDENCES_RS_SRV_UAV	2


#endif
