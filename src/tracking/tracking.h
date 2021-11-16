#pragma once

#include "rgbd_camera.h"
#include "dx/dx_texture.h"
#include "dx/dx_buffer.h"

struct depth_tracker
{
	depth_tracker();
	depth_tracker(depth_tracker&&) = default;
	depth_tracker& operator=(const depth_tracker&) = delete;
	depth_tracker& operator=(depth_tracker&&) = default;

	void update();

	rgbd_camera camera;
	ref<dx_texture> cameraDepthTexture;
	ref<dx_buffer> uploadBuffer;
};

