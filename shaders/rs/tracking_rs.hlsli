#ifndef TRACKING_RS_HLSLI
#define TRACKING_RS_HLSLI

#include "camera.hlsli"
#include "indirect.hlsli"

#define TRACKING_ICP_BLOCK_SIZE 256

struct visualize_depth_cb
{
	mat4 mvp;
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
	mat4 p;
	distortion_cb distortion;
};

#ifdef HLSL
#define tracking_correspondence_mode_camera_to_render 0
#define tracking_correspondence_mode_render_to_camera 1
#endif

struct create_correspondences_ps_cb
{
	float depthScale;
	float squaredPositionThreshold;
	float cosAngleThreshold;
	uint32 correspondenceMode;
};

struct tracking_correspondence
{
	vec4 grad0;
	vec4 grad1;
};

struct tracking_indirect
{
	D3D12_DISPATCH_ARGUMENTS initialICP;
	D3D12_DISPATCH_ARGUMENTS reduce0;
	D3D12_DISPATCH_ARGUMENTS reduce1; // This should be enough.
	uint32 counter;
};

struct tracking_ata
{
	float m[21];
};

struct tracking_atb
{
	float m[6];
};

struct tracking_ata_atb
{
	tracking_ata ata;
	tracking_atb atb;
};


#define ata_m00 0
#define ata_m01	1
#define ata_m02 2
#define ata_m03 3
#define ata_m04 4
#define ata_m05 5

#define ata_m11 6
#define ata_m12 7
#define ata_m13 8
#define ata_m14 9
#define ata_m15 10

#define ata_m22 11
#define ata_m23 12
#define ata_m24 13
#define ata_m25 14

#define ata_m33 15
#define ata_m34 16
#define ata_m35 17

#define ata_m44 18
#define ata_m45 19

#define ata_m55 20


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
	"RootConstants(num32BitConstants=40, b0, visibility=SHADER_VISIBILITY_VERTEX)"

#define CREATE_CORRESPONDENCES_RS \
	"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |" \
	"DENY_HULL_SHADER_ROOT_ACCESS |" \
	"DENY_DOMAIN_SHADER_ROOT_ACCESS |" \
	"DENY_GEOMETRY_SHADER_ROOT_ACCESS)," \
	"RootConstants(num32BitConstants=40, b0, visibility=SHADER_VISIBILITY_VERTEX), " \
	"RootConstants(num32BitConstants=4, b1, visibility=SHADER_VISIBILITY_PIXEL), " \
	"DescriptorTable(SRV(t0, numDescriptors=2), UAV(u0, numDescriptors=2), visibility=SHADER_VISIBILITY_PIXEL)"

#define CREATE_CORRESPONDENCES_RS_VS_CB		0
#define CREATE_CORRESPONDENCES_RS_PS_CB		1
#define CREATE_CORRESPONDENCES_RS_SRV_UAV	2


struct tracking_prepare_dispatch_cb
{
	uint32 minNumCorrespondences;
};


#define PREPARE_DISPATCH_RS \
	"RootFlags(0)," \
	"RootConstants(num32BitConstants=1, b0), " \
	"UAV(u0)"

#define PREPARE_DISPATCH_RS_CB				0
#define PREPARE_DISPATCH_RS_BUFFER			1
	


#define ICP_RS \
	"RootFlags(0)," \
	"SRV(t0), " \
	"SRV(t1), " \
	"UAV(u0)"

#define ICP_RS_COUNTER						0
#define ICP_RS_CORRESPONDENCES				1
#define ICP_RS_OUTPUT						2



struct tracking_icp_reduce_cb
{
	uint32 reduceIndex;
};

#define ICP_REDUCE_RS \
	"RootFlags(0)," \
	"RootConstants(num32BitConstants=1, b0), " \
	"SRV(t0), " \
	"SRV(t1), " \
	"UAV(u0)"

#define ICP_REDUCE_RS_CB					0
#define ICP_REDUCE_RS_COUNTER				1
#define ICP_REDUCE_RS_INPUT					2
#define ICP_REDUCE_RS_OUTPUT				3

#endif
