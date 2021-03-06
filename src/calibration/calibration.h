#pragma once

#include "scene/scene.h"
#include "tracking/tracking.h"
#include "projection_mapping/projector_manager.h"
#include "solver.h"


struct projector_system_calibration
{
	projector_system_calibration(depth_tracker* tracker, projector_manager* manager);
	bool edit(game_scene& scene);

	void update();

	void visualizeIntermediateResults(struct ldr_render_pass* renderPass);

private:

	bool projectCalibrationPatterns(game_scene& scene);
	bool calibrate(game_scene& scene);

	void submitPointCloudForVisualization(const struct image_point_cloud& pc, vec4 color);
	void submitFrustumForVisualization(vec3 position, quat rotation, uint32 width, uint32 height, camera_intrinsics intrinsics, vec4 color);

	bool computeInitialExtrinsicProjectorCalibrationEstimate(
		std::vector<struct pixel_correspondence> pixelCorrespondences,
		const struct image_point_cloud& renderedPointCloud,
		const camera_intrinsics& camIntrinsics, uint32 camWidth, uint32 camHeight,
		const camera_intrinsics& projIntrinsics, uint32 projWidth, uint32 projHeight,
		vec3& outPosition, quat& outRotation);

	enum calibration_state
	{
		calibration_state_uninitialized,
		calibration_state_none,
		calibration_state_projecting_patterns,
		calibration_state_calibrating,
	};

	volatile bool cancel = false;

	calibration_state state = calibration_state_uninitialized;
	depth_tracker* tracker;
	projector_manager* manager;

	float whiteValue = 0.5f;
	calibration_solver_settings solverSettings;

	camera_intrinsics startIntrinsics[projector_manager::MAX_NUM_PROJECTORS] = {};


	ref<dx_texture> depthToColorTexture;
	ref<dx_texture> depthBuffer;
	ref<dx_buffer> readbackBuffer;


	struct point_cloud_visualization
	{
		ref<dx_vertex_buffer> vertexBuffer;
		vec4 color;
	};

	struct frustum_visualization
	{
		quat rotation;
		vec3 position;
		uint32 width, height;
		camera_intrinsics intrinsics;
		vec4 color;
	};

	struct final_calibration
	{
		std::string uniqueID;
		projector_calibration calib;
	};

	std::mutex mutex;
	std::vector<point_cloud_visualization> pointCloudsToVisualize;
	std::vector<frustum_visualization> frustaToVisualize;
	std::vector<struct software_window*> windowsToClose;

	std::unordered_map<std::string, projector_calibration> finalCalibrations;
};

