#pragma once

#include "scene/scene.h"
#include "tracking/tracking.h"
#include "projection_mapping/projector_manager.h"


struct projector_system_calibration
{
	projector_system_calibration(depth_tracker* tracker, projector_manager* manager);
	bool edit();

	void visualizeIntermediateResults(struct ldr_render_pass* renderPass);

private:

	bool projectCalibrationPatterns();
	bool calibrate();

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

	struct calibration_solver_params
	{
		uint32	numIterations = 4;
		float	distanceBetweenFeatures = 0.08f;
		uint32	numNeighborsForFeatureDetection = 250;
		float	depthWeight = 1000.f;
		float	scale = 1.f;

		float	maxDistance = 0.3f;

		float	icpPercentage = 0.3f;
		float	solverPercentage = 0.15f;

		int32	startSolvingForDistortionOnIteration = -1;	// -1 means don't solve for distortion
		int32	recomputeFeaturesEveryNIterations = -1;		// -1 means never recompute
		int32	maxNumFeatureRecomputations = -1;			// -1 means recompute as often as necessary
	};

	volatile bool cancel = false;

	calibration_state state = calibration_state_uninitialized;
	depth_tracker* tracker;
	projector_manager* manager;

	float whiteValue = 0.5f;

	static constexpr uint32 MAX_NUM_PROJECTORS = 16;

	bool isProjectorIndex[MAX_NUM_PROJECTORS] = {};
	bool calibrateIndex[MAX_NUM_PROJECTORS] = {};


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

	std::mutex visualizationMutex;
	std::vector<point_cloud_visualization> pointCloudsToVisualize;
	std::vector<frustum_visualization> frustaToVisualize;
};

