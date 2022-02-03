#include "pch.h"
#include "reconstruction.h"

vec3 triangulateStereo(const camera_intrinsics& camIntr, const camera_intrinsics& projIntr, vec3 projPosition, quat projRotation, vec2 camPixel, vec2 projPixel, float& outDistance, triangulation_mode mode)
{
	vec3 camOrigin(0.f, 0.f, 0.f);

	vec3 camRay = unproject(camPixel, camIntr);
	vec3 projRay = projRotation * unproject(projPixel, projIntr);

	// Approximate ray intersection.
	float v1tv1 = dot(camRay, camRay);
	float v2tv2 = dot(projRay, projRay);
	float v1tv2 = dot(camRay, projRay);
	float v2tv1 = dot(projRay, camRay);

	float detV = v1tv1 * v2tv2 - v1tv2 * v2tv1;

	vec3 q2_q1 = projPosition - camOrigin;
	float Q1 = dot(camRay, q2_q1);
	float Q2 = -dot(projRay, q2_q1);

	float lambda1 = (v2tv2 * Q1 + v1tv2 * Q2) / detV;
	float lambda2 = (v2tv1 * Q1 + v1tv1 * Q2) / detV;

	vec3 p1 = lambda1 * camRay + camOrigin;
	vec3 p2 = lambda2 * projRay + projPosition;

	vec3 result;

	if (mode == triangulate_clamp_to_cam)
	{
		result = p1;
	}
	else if (mode == triangulate_clamp_to_proj)
	{
		result = p2;
	}
	else
	{
		assert(mode == triangulate_center_point);
		result = (p1 + p2) * 0.5f;
	}

	vec3 p1_p2 = p1 - p2;
	outDistance = length(p1_p2);

	auto mask = (lambda1 < 0.f) || (lambda2 < 0.f);
	outDistance = mask ? -1.f : outDistance;

	return result;
}
