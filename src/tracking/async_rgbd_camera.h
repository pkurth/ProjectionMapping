#pragma once

#include "rgbd_camera.h"
#include "dx/dx_texture.h"
#include "dx/dx_buffer.h"


static const DXGI_FORMAT trackingDepthFormat = DXGI_FORMAT_R16_UINT;
static const DXGI_FORMAT trackingColorFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

struct async_rgbd_camera : rgbd_camera
{
	async_rgbd_camera() {}
	~async_rgbd_camera() { shutdown(); }

	void shutdown();

	bool initializeAs(rgbd_camera_type type, uint32 deviceIndex = 0, bool alignDepthToColor = true);
	bool initializeAzure(uint32 deviceIndex = 0, bool alignDepthToColor = true);
	bool initializeRealsense(uint32 deviceIndex = 0, bool alignDepthToColor = true);


	color_bgra* colorFrame = 0;


	ref<dx_texture> depthTexture;
	ref<dx_texture> depthUnprojectTableTexture;
	ref<dx_texture> colorTexture;
	ref<dx_buffer> depthUploadBuffer;
	ref<dx_buffer> colorUploadBuffer;


private:

	void initializeInternal();

	std::thread pollingThread;
};
