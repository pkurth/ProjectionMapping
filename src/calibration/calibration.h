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

	calibration_state state = calibration_state_uninitialized;
	depth_tracker* tracker;

	float whiteValue = 0.5f;

	static constexpr uint32 MAX_NUM_PROJECTORS = 16;

	bool isProjectorIndex[MAX_NUM_PROJECTORS] = {};
	bool calibrateIndex[MAX_NUM_PROJECTORS] = {};
};

