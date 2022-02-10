#pragma once

#include "rgbd_camera.h"
#include "dx/dx_texture.h"
#include "dx/dx_buffer.h"
#include "rendering/render_pass.h"
#include "scene/scene.h"


struct tracking_data
{
	bool tracking = true;

	ref<dx_buffer> icpDispatchBuffer;
	ref<dx_buffer> correspondenceBuffer;
	ref<dx_buffer> icpDispatchReadbackBuffer;

	ref<dx_buffer> ataBuffer0;
	ref<dx_buffer> ataBuffer1;

	ref<dx_buffer> ataReadbackBuffer;

	uint32 numCorrespondences = 0;
	struct depth_tracker* tracker;
};

struct tracking_component
{
	ref<tracking_data> trackingData;
};





enum tracking_correspondence_mode
{
	tracking_correspondence_mode_camera_to_render,
	tracking_correspondence_mode_render_to_camera
};

enum tracking_rotation_representation
{
	tracking_rotation_representation_euler,
	tracking_rotation_representation_lie,
};

enum tracking_mode
{
	tracking_mode_track_object,
	tracking_mode_track_camera,
};

struct depth_tracker
{
	depth_tracker();

	void drawSettings(game_scene& scene);
	void update(game_scene& scene);
	void visualizeDepth(ldr_render_pass* renderPass);


	mat4 getTrackingMatrix(const trs& transform); // Transform relative to depth camera.
	mat4 getWorldMatrix();

	vec3 globalCameraPosition = vec3(0.f, 0.f, 0.f);
	quat globalCameraRotation = quat::identity;


	bool storeColorFrameCopy = false;
	color_bgra* colorFrameCopy = 0;

	rgbd_camera camera;

	bool disableTracking = false;

private:

	void initialize(rgbd_camera_type cameraType, uint32 deviceIndex);
	
	void initializeTrackingData(ref<tracking_data>& data);

	void processLastTrackingJobs(game_scene& scene);

	auto getTrackedObjectGroup(game_scene& scene) { return scene.group(entt::get<tracking_component, raster_component, transform_component>); }
	
	void depthPrepass(dx_command_list* cl, const raster_component& rasterComponent, const transform_component& transform);
	void createCorrespondences(dx_command_list* cl, tracking_component& trackingComponent, const raster_component& rasterComponent, const transform_component& transform);
	void accumulateCorrespondences(dx_command_list* cl, tracking_component& trackingComponent);

	ref<dx_texture> cameraDepthTexture;
	ref<dx_texture> cameraUnprojectTableTexture;
	ref<dx_texture> cameraColorTexture;
	ref<dx_buffer> depthUploadBuffer;
	ref<dx_buffer> colorUploadBuffer;

	ref<dx_texture> renderedColorTexture; // For debug window only.
	ref<dx_texture> renderedDepthTexture;

	float positionThreshold = 0.1f;
	float angleThreshold = deg2rad(45.f);

	bool showDepth = false;

	float smoothing = 0.7f; // For simple smoothing mode. Higher is smoother.

	// For old smoothing mode.
	float hRotation = 10.f;
	float hTranslation = 50.f;

	bool oldSmoothingMode = true;

	uint32 minNumCorrespondences = 5000;

	tracking_correspondence_mode correspondenceMode = tracking_correspondence_mode_camera_to_render;
	tracking_rotation_representation rotationRepresentation = tracking_rotation_representation_lie;
	tracking_mode mode = tracking_mode_track_object;

	bool tracking = false;
};

