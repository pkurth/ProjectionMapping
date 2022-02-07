#include "pch.h"
#include "solver.h"
#include "non_linear_least_squares.h"
#include "point_cloud.h"
#include "reconstruction.h"

#include "core/log.h"


struct param_set
{
	// View transform, not model!
	camera_intrinsics intrinsics;
	
	static inline vec3 rotation;
	static inline vec3 translation;
};

static constexpr uint32 numParams = sizeof(param_set) / sizeof(float);
//static_assert(numParams == 10);

struct backprojection_residual : least_squares_residual<param_set, 2>
{
	vec3 camPos;
	vec2 observedProjPixel;

	void value(const param_set& params, float out[2]) const override
	{
		camera_intrinsics i = params.intrinsics;
		vec3 translation = params.translation;

		float alpha = params.rotation.x;
		float beta = params.rotation.y;
		float gamma = params.rotation.z;

		float c1 = cos(alpha);
		float c2 = cos(beta);
		float c3 = cos(gamma);

		float s1 = sin(alpha);
		float s2 = sin(beta);
		float s3 = sin(gamma);

		mat3 r;
		r.m00 = c1 * c3 - c2 * s1 * s3;
		r.m10 = c3 * s1 + c1 * c2 * s3;
		r.m20 = s2 * s3;
		r.m01 = -c1 * s3 - c2 * c3 * s1;
		r.m11 = c1 * c2 * c3 - s1 * s3;
		r.m21 = c3 * s2;
		r.m02 = s1 * s2;
		r.m12 = -c1 * s2;
		r.m22 = c2;

		vec3 projPos = r * camPos + translation;
		vec2 projPixel = project(projPos, i);

		vec2 error = observedProjPixel - projPixel;

		out[0] = error.x;
		out[1] = error.y;
	}

	void grad(const param_set& params, float out[2][numParams]) const override
	{
		camera_intrinsics i = params.intrinsics;
		vec3 translation = params.translation;

		float alpha = params.rotation.x;
		float beta = params.rotation.y;
		float gamma = params.rotation.z;

		float c1 = cos(alpha);
		float c2 = cos(beta);
		float c3 = cos(gamma);

		float s1 = sin(alpha);
		float s2 = sin(beta);
		float s3 = sin(gamma);

		mat3 r;
		r.m00 = c1 * c3 - c2 * s1 * s3;
		r.m10 = c3 * s1 + c1 * c2 * s3;
		r.m20 = s2 * s3;
		r.m01 = -c1 * s3 - c2 * c3 * s1;
		r.m11 = c1 * c2 * c3 - s1 * s3;
		r.m21 = c3 * s2;
		r.m02 = s1 * s2;
		r.m12 = -c1 * s2;
		r.m22 = c2;

		vec3 projPos = r * camPos + translation;

		float px = projPos.x;
		float py = projPos.y;
		float pz = projPos.z;
		float pz2 = pz * pz;

		float cx = camPos.x;
		float cy = camPos.y;
		float cz = camPos.z;

		float tx = translation.x;
		float ty = translation.y;
		float tz = translation.z;

#if 1

		/*
		
		(f (cos(x) (cos(y) (u sin(z) + v cos(z)) - w sin(y)) + sin(x) (u cos(z) - v sin(z))))/                         (n + u sin(y) sin(z) + v sin(y) cos(z) + w cos(y))
		(f ((cos(y) (u sin(z) + v cos(z)) - w sin(y)) (m - sin(x) cos(y) (u sin(z) + v cos(z)) + cos(x) (u cos(z) - v sin(z)) + w sin(x) sin(y)) - sin(x) (sin(y) (u sin(z) + v cos(z)) + w cos(y)) (n + u sin(y) sin(z) + v sin(y) cos(z) + w cos(y))))/(n + u sin(y) sin(z) + v sin(y) cos(z) + w cos(y))^2
		(f ((u cos(z) - v sin(z)) (sin(y) (m + w sin(x) sin(y)) + n sin(x) cos(y) + w sin(x) cos^2(y)) + cos(x) (sin(z) (n u + (u^2 + v^2) sin(y) sin(z) + u w cos(y)) + v cos(z) (n + w cos(y)) + (u^2 + v^2) sin(y) cos^2(z))))/             (n + u sin(y) sin(z) + v sin(y) cos(z) + w cos(y))^2
		
		
		(f (sin(x) (w sin(y) - cos(y) (u sin(z) + v cos(z))) + cos(x) (u cos(z) - v sin(z))))/(n + u sin(y) sin(z) + v sin(y) cos(z) + w cos(y))
		(f (-(cos(y) (u sin(z) + v cos(z)) - w sin(y)) (m + cos(x) (cos(y) (u sin(z) + v cos(z)) - w sin(y)) + u sin(x) cos(z) - v sin(x) sin(z)) - cos(x) (sin(y) (u sin(z) + v cos(z)) + w cos(y)) (n + u sin(y) sin(z) + v sin(y) cos(z) + w cos(y))))/(n + u sin(y) sin(z) + v sin(y) cos(z) + w cos(y))^2
		-(f (sin(z) (sin(x) (n u + (u^2 + v^2) sin(y) sin(z) + u w cos(y)) - m v sin(y)) + cos(z) (m u sin(y) + v sin(x) (n + w cos(y))) - cos(x) (n cos(y) + w sin^2(y) + w cos^2(y)) (u cos(z) - v sin(z)) + (u^2 + v^2) sin(x) sin(y) cos^2(z)))/(n + u sin(y) sin(z) + v sin(y) cos(z) + w cos(y))^2
		*/


		// Intrinsics.
		out[0][0] = -px / pz;
		out[0][1] = 0.f;
		out[0][2] = 1.f;
		out[0][3] = 0.f;

		if constexpr (numParams > 4)
		{
			// Rotation.
			out[0][4] = (i.fx * (c1 * (c2 * (cx * s3 + cy * c3) - pz * s2) + s1 * (cx * c3 - cy * s3))) / pz;
			out[0][5] = (i.fx * ((c2 * (cx * s3 + cy * c3) - pz * s2) * (tx - s1 * c2 * (cx * s3 + cy * c3) + c1 * (cx * c3 - cy * s3) + cz * s1 * s2) - s1 * (s2 * (cx * s3 + cy * c3) + cz * c2) * (tz + cx * s2 * s3 + cy * s2 * c3 + cz * c2))) / pz2;
			out[0][6] = (i.fx * ((cx * c3 - cy * s3) * (s2 * (tx + pz * s1 * s2) + tz * s1 * c2 + cz * s1 * c2 * c2) + c1 * (s3 * (tz * cx + (cx * cx + cy * cy) * s2 * s3 + cx * cz * c2) + cy * c3 * (tz + cz * c2) + (cx * cx + cy * cy) * s2 * c2 * c2))) / pz2;

			if constexpr (numParams > 7)
			{
				// Translation.
				out[0][7] = -i.fx / pz;
				out[0][8] = 0.f;
				out[0][9] = i.fx * px / pz2;
			}
		}


		// Intrinsics.
		out[1][0] = 0.f;
		out[1][1] = py / pz;
		out[1][2] = 0.f;
		out[1][3] = 1.f;

		if constexpr (numParams > 4)
		{
			// Rotation.
			out[1][4] = (i.fy * (s1 * (cz * s2 - c2 * (cx * s3 + cy * c3)) + c1 * (cx * c3 - cy * s3))) / pz;
			out[1][5] = (i.fy * (-(c2 * (cx * s3 + cy * c3) - cz * s2) * (ty + c1 * (c2 * (cx * s3 + cy * c3) - cz * s2) + cx * s1 * c3 - cy * s1 * s3) - c1 * (s2 * (cx * s3 + cy * c3) + cz * c2) * (tz + cx * s2 * s3 + cy * s2 * c3 + cz * c2))) / pz2;
			out[1][6] = -(i.fy * (s3 * (s1 * (tz * cx + (cx * cx + cy * cy) * s2 * s3 + cx * cz * c2) - ty * cy * s2) + c3 * (ty * cx * s2 + cy * s1 * (tz + cz * c2)) - c1 * (tz * c2 + cz * s2 * s2 + cz * c2 * c2) * (cx * c3 - cy * s3) + (cx * cx + cy * cy) * s1 * s2 * c3 * c3)) / pz2;

			if constexpr (numParams > 7)
			{
				// Translation.
				out[1][7] = 0.f;
				out[1][8] = i.fy / pz;
				out[1][9] = -i.fy * py / pz2;
			}
		}
#endif
	}
};

void solveForCameraToProjectorParameters(const image_point_cloud& renderedPC, const image<vec2>& pixelCorrespondences, 
	vec3& projPosition, quat& projRotation, camera_intrinsics& projIntrinsics)
{
	quat R = conjugate(projRotation);
	vec3 t = -(R * projPosition);



	mat3 mat = quaternionToMat3(R);
	float alpha = atan2(mat.m02, -mat.m12);
	float beta = atan2(sqrt(1.f - mat.m22 * mat.m22), mat.m22);
	float gamma = atan2(mat.m20, mat.m21);


	param_set params;
	params.intrinsics = projIntrinsics;
	params.rotation = vec3(alpha, beta, gamma);
	params.translation = t;

	backprojection_residual* residuals = new backprojection_residual[renderedPC.numEntries];

	uint32 numResiduals = 0;
	for (uint32 y = 0, i = 0; y < renderedPC.entries.height; ++y)
	{
		for (uint32 x = 0; x < renderedPC.entries.width; ++x, ++i)
		{
			const auto& e = renderedPC.entries.data[i];
			vec2 proj = pixelCorrespondences(y, x);

			if (e.position.z != 0.f && validPixel(proj))
			{
				residuals[numResiduals].camPos = e.position;
				residuals[numResiduals].observedProjPixel = proj;
				++numResiduals;
			}
		}
	}

	levenberg_marquardt_settings settings;

	LOG_MESSAGE("Solving for projector parameters with %u residuals", numResiduals);

	levenbergMarquardt(settings, params, residuals, numResiduals);

	LOG_MESSAGE("Solver finished");

	quat oldRotation = projRotation;
	vec3 oldPosition = projPosition;
	camera_intrinsics oldIntrinsics = projIntrinsics;




	alpha = params.rotation.x;
	beta = params.rotation.y;
	gamma = params.rotation.z;

	float c1 = cos(alpha);
	float c2 = cos(beta);
	float c3 = cos(gamma);

	float s1 = sin(alpha);
	float s2 = sin(beta);
	float s3 = sin(gamma);

	mat3 r;
	r.m00 = c1 * c3 - c2 * s1 * s3;
	r.m10 = c3 * s1 + c1 * c2 * s3;
	r.m20 = s2 * s3;
	r.m01 = -c1 * s3 - c2 * c3 * s1;
	r.m11 = c1 * c2 * c3 - s1 * s3;
	r.m21 = c3 * s2;
	r.m02 = s1 * s2;
	r.m12 = -c1 * s2;
	r.m22 = c2;



	projRotation = conjugate(mat3ToQuaternion(r));
	projPosition = -(projRotation * params.translation);
	projIntrinsics = params.intrinsics;

	LOG_MESSAGE("Final projector intrinsics: [%.3f, %.3f, %.3f, %.3f]", projIntrinsics.fx, projIntrinsics.fy, projIntrinsics.cx, projIntrinsics.cy);
	LOG_MESSAGE("Final projector position: [%.3f, %.3f, %.3f]", projPosition.x, projPosition.y, projPosition.z);
	LOG_MESSAGE("Final projector rotation: [%.3f, %.3f, %.3f, %.3f]", projRotation.x, projRotation.y, projRotation.z, projRotation.w);

	delete[] residuals;
}


