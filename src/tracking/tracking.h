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

enum tracking_rotation_representation
{
	tracking_rotation_representation_euler,
	tracking_rotation_representation_lie,
};

enum tracker_ui_interaction
{
	tracker_ui_no_interaction,
	tracker_ui_select_tracked_entity,
};

struct depth_tracker
{
	depth_tracker();
	depth_tracker(depth_tracker&&) = default;
	depth_tracker& operator=(const depth_tracker&) = delete;
	depth_tracker& operator=(depth_tracker&&) = default;

	tracker_ui_interaction drawSettings();
	void update();
	void visualizeDepth(ldr_render_pass* renderPass);

	struct solver_stats
	{
		float error;
		uint32 numCGIterations;
	};

	scene_entity trackedEntity = {};

	vec3 globalCameraPosition = vec3(0.f, 0.f, 0.f);
	quat globalCameraRotation = quat::identity;

private:

	bool cameraInitialized() { return cameraDepthTexture != 0; }

	void initialize(rgbd_camera_type cameraType, uint32 deviceIndex);

	rgbd_camera camera;

	ref<dx_texture> cameraDepthTexture;
	ref<dx_texture> cameraUnprojectTableTexture;
	ref<dx_texture> cameraColorTexture;
	ref<dx_buffer> depthUploadBuffer;
	ref<dx_buffer> colorUploadBuffer;

	ref<dx_texture> renderedColorTexture; // For debug window only.
	ref<dx_texture> renderedDepthTexture;

	ref<dx_buffer> icpDispatchBuffer;
	ref<dx_buffer> correspondenceBuffer;
	ref<dx_buffer> icpDispatchReadbackBuffer;

	ref<dx_buffer> ataBuffer0;
	ref<dx_buffer> ataBuffer1;

	ref<dx_buffer> ataReadbackBuffer;

	float positionThreshold = 0.1f;
	float angleThreshold = deg2rad(45.f);

	bool showDepth = true;

	float smoothing = 0.7f; // For simple smoothing mode. Higher is smoother.

	// For old smoothing mode.
	float hRotation = 10.f;
	float hTranslation = 50.f;

	bool oldSmoothingMode = true;

	uint32 minNumCorrespondences = 5000;

	tracking_direction trackingDirection = tracking_direction_camera_to_render;
	tracking_rotation_representation rotationRepresentation = tracking_rotation_representation_lie;

	bool tracking = false;


	// This is declared here, so that we can show it in the editor.
	solver_stats stats = {};
	uint32 numCorrespondences = 0;
};

