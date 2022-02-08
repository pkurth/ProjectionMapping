#pragma once

#include "core/math.h"
#include "core/camera.h"
#include "calibration_image.h"
#include "tracking/rgbd_camera.h"

struct point_cloud_entry
{
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
	image<uint8> createValidMask();

	bool writeToImage(const fs::path& path);
	bool writeToFile(const fs::path& path);
};
