#pragma once

#include "core/math.h"
#include "core/camera.h"
#include "core/image.h"
#include "tracking/rgbd_camera.h"

struct point_cloud_entry
{
	vec3 position;
	vec3 normal;
};

struct image_point_cloud
{
	image<point_cloud_entry> entries;
	image<uint8> validPixelMask;
	uint32 numEntries;

	void constructFromRendering(const image<vec4>& rendering, const image<vec2>& unprojectTable);
	void erode(uint32 numIterations);

	bool writeToImage(const fs::path& path);
	bool writeToFile(const fs::path& path);
};
