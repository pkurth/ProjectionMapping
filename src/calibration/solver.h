#pragma once

#include "calibration_image.h"
#include "core/camera.h"

struct calibration_solver_settings
{
	float percentageOfCorrespondencesToUse = 0.2f;
	uint32 maxNumIterations = 300;
};

struct calibration_solver_input
{
	calibration_solver_input(const struct image_point_cloud& renderedPC, const image<vec2>& pixelCorrespondences)
		: renderedPC(renderedPC), pixelCorrespondences(pixelCorrespondences) {}

	const struct image_point_cloud& renderedPC;
	const image<vec2>& pixelCorrespondences;
};

void solveForCameraToProjectorParameters(const std::vector<calibration_solver_input>& input, 
	vec3& projPosition, quat& projRotation, camera_intrinsics& projIntrinsics,
	calibration_solver_settings settings);
