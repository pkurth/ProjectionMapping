#ifndef CALIBRATION_RS_HLSLI
#define CALIBRATION_RS_HLSLI

#include "camera.hlsli"

struct depth_to_color_cb
{
	mat4 mv;
	mat4 p;
	distortion_cb distortion;
};

#define DEPTH_TO_COLOR_RS \
	"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |" \
	"DENY_HULL_SHADER_ROOT_ACCESS |" \
	"DENY_DOMAIN_SHADER_ROOT_ACCESS |" \
	"DENY_GEOMETRY_SHADER_ROOT_ACCESS |" \
	"DENY_PIXEL_SHADER_ROOT_ACCESS)," \
	"RootConstants(num32BitConstants=40, b0, visibility=SHADER_VISIBILITY_VERTEX)"

#define DEPTH_TO_COLOR_RS_CB 0

#endif
