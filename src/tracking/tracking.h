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

struct depth_tracker
{
	depth_tracker();
	depth_tracker(depth_tracker&&) = default;
	depth_tracker& operator=(const depth_tracker&) = delete;
	depth_tracker& operator=(depth_tracker&&) = default;

	scene_entity drawSettings();
	void update();
	void visualizeDepth(ldr_render_pass* renderPass);

	bool isEntityTracked(scene_entity entity);
	bool trackEntity(scene_entity entity);
	void clearTrackedEntities();

	uint32 getNumberOfTrackedEntities() { return numTrackingJobs; }
	scene_entity getTrackedEntity(uint32 index);
	mat4 getTrackingMatrix(uint32 index);

	vec3 globalCameraPosition = vec3(0.f, 0.f, 0.f);
	quat globalCameraRotation = quat::identity;


	bool storeColorFrameCopy = false;
	color_bgra* colorFrameCopy = 0;

	rgbd_camera camera;

private:

	void initialize(rgbd_camera_type cameraType, uint32 deviceIndex);
	
	struct tracking_job
	{
		ref<dx_buffer> icpDispatchBuffer;
		ref<dx_buffer> correspondenceBuffer;
		ref<dx_buffer> icpDispatchReadbackBuffer;

		ref<dx_buffer> ataBuffer0;
		ref<dx_buffer> ataBuffer1;

		ref<dx_buffer> ataReadbackBuffer;

		scene_entity trackedEntity;

		bool buffersAreInitialized = false;
		bool used = false;

		uint32 numCorrespondences;
	};

	void initializeTrackingJob(tracking_job& job, scene_entity entity);
	void processLastTrackingJob(tracking_job& job);
	void depthPrepass(dx_command_list* cl, tracking_job& job);
	void createCorrespondences(dx_command_list* cl, tracking_job& job);

	ref<dx_texture> cameraDepthTexture;
	ref<dx_texture> cameraUnprojectTableTexture;
	ref<dx_texture> cameraColorTexture;
	ref<dx_buffer> depthUploadBuffer;
	ref<dx_buffer> colorUploadBuffer;

	ref<dx_texture> renderedColorTexture; // For debug window only.
	ref<dx_texture> renderedDepthTexture;

	tracking_job trackingJobs[16];
	uint32 numTrackingJobs = 0;

	float positionThreshold = 0.1f;
	float angleThreshold = deg2rad(45.f);

	bool showDepth = false;

	float smoothing = 0.7f; // For simple smoothing mode. Higher is smoother.

	// For old smoothing mode.
	float hRotation = 10.f;
	float hTranslation = 50.f;

	bool oldSmoothingMode = true;

	uint32 minNumCorrespondences = 5000;

	tracking_direction trackingDirection = tracking_direction_camera_to_render;
	tracking_rotation_representation rotationRepresentation = tracking_rotation_representation_lie;

	bool tracking = false;
};

