#pragma once

#include "rgbd_camera.h"
#include "dx/dx_texture.h"
#include "dx/dx_buffer.h"
#include "rendering/render_pass.h"
#include "scene/scene.h"

struct depth_tracker
{
	depth_tracker();
	depth_tracker(depth_tracker&&) = default;
	depth_tracker& operator=(const depth_tracker&) = delete;
	depth_tracker& operator=(depth_tracker&&) = default;

	void trackObject(scene_entity entity);

	void update();
	void visualizeDepth(ldr_render_pass* renderPass);

	rgbd_camera camera;

	ref<dx_texture> cameraDepthTexture;
	ref<dx_texture> cameraUnprojectTableTexture;
	ref<dx_texture> cameraColorTexture;
	ref<dx_buffer> depthUploadBuffer;
	ref<dx_buffer> colorUploadBuffer;

	ref<dx_texture> renderedColorTexture; // Temporary.
	ref<dx_texture> renderedDepthTexture;

	scene_entity trackedEntity = {};
};

