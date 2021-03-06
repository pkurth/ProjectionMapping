#include "pch.h"
#include "solver.h"
#include "non_linear_least_squares.h"
#include "point_cloud.h"
#include "reconstruction.h"
#include "math_double.h"
#include "graycode.h"

#include "core/random.h"
#include "core/log.h"

struct camera_intrinsicsd
{
	double fx, fy, cx, cy;
};

static vec2d project(const vec3d& p, const camera_intrinsicsd& intr)
{
	double x = -p.x / p.z;
	double y = p.y / p.z;

	x = intr.fx * x + intr.cx;
	y = intr.fy * y + intr.cy;

	return vec2d{ x, y };
}

struct param_set
{
	camera_intrinsicsd intrinsics;

	// View transform, not model!
	vec3d rotation;
	vec3d translation;
};

static constexpr uint32 numParams = sizeof(param_set) / sizeof(double);

struct precompute_data
{
	double c1;
	double c2;
	double c3;
	double s1;
	double s2;
	double s3;

	mat3d r;

	void precompute(const param_set& params)
	{
		double alpha = params.rotation.x;
		double beta = params.rotation.y;
		double gamma = params.rotation.z;

		c1 = cos(alpha);
		c2 = cos(beta);
		c3 = cos(gamma);

		s1 = sin(alpha);
		s2 = sin(beta);
		s3 = sin(gamma);

		r.m00 = c1 * c3 - c2 * s1 * s3;
		r.m10 = c3 * s1 + c1 * c2 * s3;
		r.m20 = s2 * s3;
		r.m01 = -c1 * s3 - c2 * c3 * s1;
		r.m11 = c1 * c2 * c3 - s1 * s3;
		r.m21 = c3 * s2;
		r.m02 = s1 * s2;
		r.m12 = -c1 * s2;
		r.m22 = c2;
	}
};

struct backprojection_residual : least_squares_residual<param_set, 2, precompute_data>
{
	vec3d camPos;
	vec2d observedProjPixel;

	void value(const param_set& params, const precompute_data& precompute, double out[2]) const
	{
		camera_intrinsicsd i = params.intrinsics;
		vec3d translation = params.translation;

		vec3d projPos = precompute.r * camPos + translation;
		vec2d projPixel = project(projPos, i);

		vec2d error = observedProjPixel - projPixel;

		out[0] = error.x;
		out[1] = error.y;
	}

	void grad(const param_set& params, const precompute_data& precompute, double out[2][numParams]) const
	{
		camera_intrinsicsd i = params.intrinsics;
		vec3d translation = params.translation;

		double c1 = precompute.c1;
		double c2 = precompute.c2;
		double c3 = precompute.c3;

		double s1 = precompute.s1;
		double s2 = precompute.s2;
		double s3 = precompute.s3;

		vec3d projPos = precompute.r * camPos + translation;

		double px = projPos.x;
		double py = projPos.y;
		double pz = projPos.z;
		double pz2 = pz * pz;

		double cx = camPos.x;
		double cy = camPos.y;
		double cz = camPos.z;

		double tx = translation.x;
		double ty = translation.y;
		double tz = translation.z;

		/*
		* In the formulas:
		* - x,y,z = euler angles
		* - f = fx/fy
		* - u,v,w = camPos components
		* - m = tx/ty
		* - n = tz
		* - c = cx/cy
		* 
		* e = -fx * camPos.x / camPos.z + cx
		* https://www.wolframalpha.com/input?i=differentiate+-f+*+%28cos%28x%29cos%28z%29u+-+cos%28y%29sin%28x%29sin%28z%29u+-+cos%28x%29sin%28z%29v+-+cos%28y%29cos%28z%29sin%28x%29v+%2B+sin%28x%29sin%28y%29w+%2B+m%29+%2F+%28sin%28y%29sin%28z%29u+%2B+cos%28z%29sin%28y%29v+%2B+cos%28y%29w+%2B+n%29+%2B+c
		* 
		* e = fy * camPos.y / camPos.z + cy
		* https://www.wolframalpha.com/input?i=differentiate+f+*+%28cos%28z%29sin%28x%29u+%2B+cos%28x%29cos%28y%29sin%28z%29u+%2B+cos%28x%29cos%28y%29cos%28z%29v+-+sin%28x%29sin%28z%29v+-+cos%28x%29sin%28y%29w+%2B+m%29+%2F+%28sin%28y%29sin%28z%29u+%2B+cos%28z%29sin%28y%29v+%2B+cos%28y%29w+%2B+n%29+%2B+c
		* 
		
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
	}
};

void solveForCameraToProjectorParameters(const std::vector<calibration_solver_input>& input,
	vec3& projPosition, quat& projRotation, camera_intrinsics& projIntrinsics,
	calibration_solver_settings settings)
{
	quat R = conjugate(projRotation);
	vec3 t = -(R * projPosition);



	mat3 mat = quaternionToMat3(R);
	double alpha = atan2(mat.m02, -mat.m12);
	double beta = atan2(sqrt(1.f - mat.m22 * mat.m22), mat.m22);
	double gamma = atan2(mat.m20, mat.m21);


	param_set params;
	params.intrinsics = { projIntrinsics.fx, projIntrinsics.fy, projIntrinsics.cx, projIntrinsics.cy };
	params.rotation = { alpha, beta, gamma };
	params.translation = { t.x, t.y, t.z };

	uint32 expectedNumResiduals = 0;
	for (const calibration_solver_input& in : input)
	{
		expectedNumResiduals += in.renderedPC.numEntries;
	}

	expectedNumResiduals = (uint32)(expectedNumResiduals * settings.percentageOfCorrespondencesToUse * 2); // Times 2 just to be safe.

	std::vector<backprojection_residual> residuals;
	residuals.reserve(expectedNumResiduals);

	random_number_generator rng = { 61923 };

	for (const calibration_solver_input& in : input)
	{
		for (uint32 y = 0, i = 0; y < in.renderedPC.entries.height; ++y)
		{
			for (uint32 x = 0; x < in.renderedPC.entries.width; ++x, ++i)
			{
				const auto& e = in.renderedPC.entries.data[i];
				vec2 proj = in.pixelCorrespondences(y, x);

				if (e.position.z != 0.f && validPixel(proj))
				{
					if (rng.randomFloat01() < settings.percentageOfCorrespondencesToUse)
					{
						backprojection_residual r;
						r.camPos = { e.position.x, e.position.y, e.position.z };
						r.observedProjPixel = { proj.x, proj.y };

						residuals.push_back(r);
					}
				}
			}
		}
	}

	levenberg_marquardt_settings lmSettings;
	lmSettings.maxNumIterations = settings.maxNumIterations;
	lmSettings.numCGIterations = 100;

	uint32 numResiduals = (uint32)residuals.size();

	LOG_MESSAGE("Solving for projector parameters with %u residuals", numResiduals);

	levenberg_marquardt_result lmResult = levenbergMarquardt(lmSettings, params, residuals.data(), numResiduals);

	LOG_MESSAGE("Solver finished");


	alpha = params.rotation.x;
	beta = params.rotation.y;
	gamma = params.rotation.z;

	float c1 = (float)cos(alpha);
	float c2 = (float)cos(beta);
	float c3 = (float)cos(gamma);

	float s1 = (float)sin(alpha);
	float s2 = (float)sin(beta);
	float s3 = (float)sin(gamma);

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
	projPosition = -(projRotation * vec3((float)params.translation.x, (float)params.translation.y, (float)params.translation.z));
	projIntrinsics = { (float)params.intrinsics.fx, (float)params.intrinsics.fy, (float)params.intrinsics.cx, (float)params.intrinsics.cy };

	LOG_MESSAGE("Solver finished after %u iterations. Remaining error: %f (avg %f)", lmResult.numIterations, lmResult.epsilon, lmResult.epsilon / numResiduals);
	LOG_MESSAGE("Final projector intrinsics: [%.3f, %.3f, %.3f, %.3f]", projIntrinsics.fx, projIntrinsics.fy, projIntrinsics.cx, projIntrinsics.cy);
	LOG_MESSAGE("Final projector position: [%.3f, %.3f, %.3f]", projPosition.x, projPosition.y, projPosition.z);
	LOG_MESSAGE("Final projector rotation: [%.3f, %.3f, %.3f, %.3f]", projRotation.x, projRotation.y, projRotation.z, projRotation.w);
}


