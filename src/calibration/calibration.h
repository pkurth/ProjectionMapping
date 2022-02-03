#pragma once

#include "scene/scene.h"
#include "tracking/tracking.h"


struct projector_system_calibration
{
	projector_system_calibration(depth_tracker* tracker);
	bool edit();

private:

	bool projectCalibrationPatterns();
	bool calibrate();

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

	float whiteValue = 0.5f;

	static constexpr uint32 MAX_NUM_PROJECTORS = 16;

	bool isProjectorIndex[MAX_NUM_PROJECTORS] = {};
	bool calibrateIndex[MAX_NUM_PROJECTORS] = {};


	ref<dx_texture> depthToColorTexture;
	ref<dx_texture> depthBuffer;
	ref<dx_buffer> readbackBuffer;
};

