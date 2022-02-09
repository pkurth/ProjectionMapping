#pragma once

#include "core/image.h"
#include "core/camera.h"
#include "solver.h"

void solveForCameraToProjectorParametersUsingCeres(const std::vector<calibration_solver_input>& input,
	vec3& projPosition, quat& projRotation, camera_intrinsics& projIntrinsics,
	calibration_solver_settings settings);

