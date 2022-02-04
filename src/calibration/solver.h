#pragma once

#include "calibration_internal.h"
#include "core/camera.h"

void solveForCameraToProjectorParameters(const struct image_point_cloud& renderedPC, const image<vec2>& pixelCorrespondences, 
	vec3& projPosition, quat& projRotation, camera_intrinsics& projIntrinsics);
