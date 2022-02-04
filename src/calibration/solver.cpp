#include "pch.h"
#include "solver.h"
#include "non_linear_least_squares.h"
#include "point_cloud.h"
#include "reconstruction.h"

#include "core/log.h"


struct param_set
{
	// View transform, not model!
	vec3 rotation;
	vec3 translation;
	camera_intrinsics intrinsics;
};

struct backprojection_residual : least_squares_residual<param_set, 2>
{
	vec3 camPos;
	vec2 observedProjPixel;

	void value(const param_set& params, float out[2]) const override
	{
		quat rotation = eulerToQuat(params.rotation);

		vec3 projPos = rotation * camPos + params.translation;
		vec2 projPixel = project(projPos, params.intrinsics);

		vec2 error = projPixel - observedProjPixel;

		out[0] = error.x;
		out[1] = error.y;
	}

	void grad(const param_set& params, float out[2][10]) const override
	{
		camera_intrinsics i = params.intrinsics;
		quat R = eulerToQuat(params.rotation);
		vec3 translation = params.translation;

		vec3 projPos = R * camPos + translation;

		float px = projPos.x;
		float py = projPos.y;
		float pz = projPos.z;
		float pz2 = pz * pz;

		float cx = camPos.x;
		float cy = camPos.y;
		float cz = camPos.z;


		float ex = 0.5f * params.rotation.x;
		float ey = 0.5f * params.rotation.y;
		float ez = 0.5f * params.rotation.z;

		float sex = sin(ex);
		float cex = sin(ex);
		float sey = sin(ey);
		float cey = sin(ey);
		float sez = sin(ez);
		float cez = sin(ez);

		float Rx = R.x;
		float Ry = R.y;
		float Rz = R.z;
		float Rw = R.w;

		float Rx_x = 0.5f * (sey * cez * -sex - cey * sez * cex);
		float Ry_x = 0.5f * (cey * sez * -sex + sey * cez * cex);
		float Rz_x = 0.5f * (cey * cez * cex - sey * sez * -sex);
		float Rw_x = 0.5f * (cey * cez * -sex + sey * sez * cex);

		float Rx_y = 0.5f * (cey * cez * cex - -sey * sez * sex);
		float Ry_y = 0.5f * (-sey * sez * cex + cey * cez * sex);
		float Rz_y = 0.5f * (-sey * cez * sex - cey * sez * cex);
		float Rw_y = 0.5f * (-sey * cez * cex + cey * sez * sex);

		float Rx_z = 0.5f * (sey * -sez * cex - cey * cez * sex);
		float Ry_z = 0.5f * (cey * cez * cex + sey * -sez * sex);
		float Rz_z = 0.5f * (cey * -sez * sex - sey * cez * cex);
		float Rw_z = 0.5f * (cey * -sez * cex + sey * cez * sex);

		// For input (x, y, z) this macro computes: cx * Ry * Rz_x + cx * Ry_x + Rz.
#define t_x(ci, R0i, R1i) (c##ci * (R##R0i * R##R1i##_x + R##R0i##_x * R##R1i))
#define t_y(ci, R0i, R1i) (c##ci * (R##R0i * R##R1i##_y + R##R0i##_y * R##R1i))
#define t_z(ci, R0i, R1i) (c##ci * (R##R0i * R##R1i##_z + R##R0i##_z * R##R1i))

		float px_x = t_x(x, w, w) + t_x(z, y, w) - t_x(y, z, w) + t_x(x, x, x) + t_x(y, x, y) + t_x(z, x, z) 
			- t_x(y, z, w) - t_x(x, z, z) + t_x(z, x, z) + t_x(z, y, w) + t_x(y, x, y) - t_x(x, y, y);

		float py_x = t_x(y, w, w) + t_x(x, z, w) - t_x(z, x, w) + t_x(x, x, y) + t_x(y, y, y) + t_x(z, y, z)
			- t_x(z, x, w) - t_x(y, x, x) + t_x(x, x, y) + t_x(x, z, w) + t_x(z, y, z) - t_x(y, z, z);

		float pz_x = t_x(z, w, w) + t_x(y, x, w) - t_x(x, y, w) + t_x(x, x, z) + t_x(y, y, z) + t_x(z, z, z)
			- t_x(x, y, w) - t_x(z, y, y) + t_x(y, y, z) + t_x(y, x, w) + t_x(x, x, z) - t_x(z, x, x);

		float px_y = t_y(x, w, w) + t_y(z, y, w) - t_y(y, z, w) + t_y(x, x, x) + t_y(y, x, y) + t_y(z, x, z)
			- t_y(y, z, w) - t_y(x, z, z) + t_y(z, x, z) + t_y(z, y, w) + t_y(y, x, y) - t_y(x, y, y);

		float py_y = t_y(y, w, w) + t_y(x, z, w) - t_y(z, x, w) + t_y(x, x, y) + t_y(y, y, y) + t_y(z, y, z)
			- t_y(z, x, w) - t_y(y, x, x) + t_y(x, x, y) + t_y(x, z, w) + t_y(z, y, z) - t_y(y, z, z);

		float pz_y = t_y(z, w, w) + t_y(y, x, w) - t_y(x, y, w) + t_y(x, x, z) + t_y(y, y, z) + t_y(z, z, z)
			- t_y(x, y, w) - t_y(z, y, y) + t_y(y, y, z) + t_y(y, x, w) + t_y(x, x, z) - t_y(z, x, x);

		float px_z = t_z(x, w, w) + t_z(z, y, w) - t_z(y, z, w) + t_z(x, x, x) + t_z(y, x, y) + t_z(z, x, z)
			- t_z(y, z, w) - t_z(x, z, z) + t_z(z, x, z) + t_z(z, y, w) + t_z(y, x, y) - t_z(x, y, y);

		float py_z = t_z(y, w, w) + t_z(x, z, w) - t_z(z, x, w) + t_z(x, x, y) + t_z(y, y, y) + t_z(z, y, z)
			- t_z(z, x, w) - t_z(y, x, x) + t_z(x, x, y) + t_z(x, z, w) + t_z(z, y, z) - t_z(y, z, z);

		float pz_z = t_z(z, w, w) + t_z(y, x, w) - t_z(x, y, w) + t_z(x, x, z) + t_z(y, y, z) + t_z(z, z, z)
			- t_z(x, y, w) - t_z(z, y, y) + t_z(y, y, z) + t_z(y, x, w) + t_z(x, x, z) - t_z(z, x, x);

#undef t_x
#undef t_y
#undef t_z

		// First output.
		out[0][0] = -i.fx / pz2 * (pz * px_x - pz_x * px);			// Rotation X.
		out[0][1] = -i.fx / pz2 * (pz * px_y - pz_y * px);			// Rotation Y.
		out[0][2] = -i.fx / pz2 * (pz * px_z - pz_z * px);			// Rotation Z.
		out[0][3] = i.fx * -translation.x / pz;						// Translation X.
		out[0][4] = 0.f;											// Translation Y.
		out[0][5] = i.fx * px / pz2;								// Translation Z.
		out[0][6] = -px / pz;										// Fx.
		out[0][7] = 0.f;											// Fy.
		out[0][8] = 1.f;											// Cx.
		out[0][9] = 0.f;											// Cy.

		// Second output.
		out[1][0] = i.fx / pz2 * (pz * py_x - pz_x * py);			// Rotation X.
		out[1][1] = i.fx / pz2 * (pz * py_y - pz_y * py);			// Rotation Y.
		out[1][2] = i.fx / pz2 * (pz * py_z - pz_z * py);			// Rotation Z.
		out[1][3] = 0.f;											// Translation X.
		out[1][4] = i.fy * translation.y / pz;						// Translation Y.
		out[1][5] = -i.fy * py / pz2;								// Translation Z.
		out[1][6] = 0.f;											// Fx.
		out[1][7] = py / pz;										// Fy.
		out[1][8] = 0.f;											// Cx.
		out[1][9] = 1.f;											// Cy.
	}
};

void solveForCameraToProjectorParameters(const image_point_cloud& renderedPC, const image<vec2>& pixelCorrespondences, 
	vec3& projPosition, quat& projRotation, camera_intrinsics& projIntrinsics)
{
	quat R = conjugate(projRotation);
	vec3 t = -(R * projPosition);

	param_set params = { quatToEuler(R), t, projIntrinsics };

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

	projRotation = conjugate(eulerToQuat(params.rotation));
	projPosition = -(projRotation * params.translation);
	projIntrinsics = params.intrinsics;

	LOG_MESSAGE("Final projector intrinsics: [%.3f, %.3f, %.3f, %.3f]", projIntrinsics.fx, projIntrinsics.fy, projIntrinsics.cx, projIntrinsics.cy);
}


