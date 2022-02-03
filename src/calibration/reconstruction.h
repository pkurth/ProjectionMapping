#pragma once

#include "core/camera.h"

enum triangulation_mode
{
	triangulate_center_point,
	triangulate_clamp_to_cam,
	triangulate_clamp_to_proj,
};

static vec2 project(const vec3& p, const camera_intrinsics& intr)
{
	float x = -p.x / p.z;
	float y = p.y / p.z;

	x = intr.fx * x + intr.cx;
	y = intr.fy * y + intr.cy;

	return vec2(x, y);
}

static vec3 unproject(vec2 p, const camera_intrinsics& intr)
{
	float x = (p.x - intr.cx) / intr.fx;
	float y = (p.y - intr.cy) / intr.fy;

	return vec3(x, -y, -1.f);
}

vec3 triangulateStereo(const camera_intrinsics& camIntr, const camera_intrinsics& projIntr,
	vec3 projPosition, quat projRotation, vec2 camPixel, vec2 projPixel, float& outDistance, triangulation_mode mode = triangulate_clamp_to_cam);
