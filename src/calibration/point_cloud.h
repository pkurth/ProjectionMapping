#pragma once

#include "core/math.h"
#include "core/camera.h"
#include "calibration_internal.h"
#include "tracking/rgbd_camera.h"

enum triangulation_mode
{
	triangulate_center_point,
	triangulate_clamp_to_cam,
	triangulate_clamp_to_proj,
};

struct point_cloud_entry
{
	vec2 cam;
	vec2 proj;

	vec3 position;
	vec3 normal;
};

struct point_cloud
{
	std::vector<point_cloud_entry> entries;
};

struct image_point_cloud
{
	image<point_cloud_entry> entries;
	uint32 numEntries;

	void constructFromRendering(const image<vec4>& rendering, const image<vec2>& unprojectTable);

	bool writeToImage(const fs::path& path);
	bool writeToFile(const fs::path& path);
};
