#pragma once

#include "rgbd_camera.h"
#include "dx/dx_texture.h"
#include "dx/dx_buffer.h"
#include "rendering/render_pass.h"
#include "scene/scene.h"

enum tracking_direction
{
	tracking_direction_camera_to_render,
	tracking_direction_render_to_camera
};

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

	ref<dx_buffer> icpDispatchBuffer;
	ref<dx_buffer> correspondenceBuffer;
	ref<dx_buffer> icpDispatchReadbackBuffer; // Temporary.

	ref<dx_buffer> ataBuffer0;
	ref<dx_buffer> ataBuffer1;

	ref<dx_buffer> ataReadbackBuffer;

	scene_entity trackedEntity = {};

	float positionThreshold = 0.1f;
	float angleThreshold = deg2rad(45.f);

	tracking_direction trackingDirection = tracking_direction_camera_to_render;
};

