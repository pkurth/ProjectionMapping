#include "pch.h"
#include "solver_ceres.h"
#include "math_double.h"
#include "point_cloud.h"
#include "reconstruction.h"
#include "graycode.h"

#include "core/random.h"

#undef arraysize
#define GLOG_NO_ABBREVIATED_SEVERITIES
#include <ceres/ceres.h>
#include <eigen/dense>
#include <eigen/geometry>


template <typename T> using evec2 = Eigen::Matrix<T, 2, 1>;
template <typename T> using evec3 = Eigen::Matrix<T, 3, 1>;
template <typename T> using evec4 = Eigen::Matrix<T, 4, 1>;

template <typename T> using emat2 = Eigen::Matrix<T, 2, 2>;
template <typename T> using emat3 = Eigen::Matrix<T, 3, 3>;
template <typename T> using emat4 = Eigen::Matrix<T, 4, 4>;

template <typename T> using equat = Eigen::Quaternion<T>;


template <typename T>
struct intrinsics
{
	T fx, fy, cx, cy;
};

template <typename T>
static evec2<T> project(const evec3<T>& p, const intrinsics<T>& intr)
{
	T x = -p.x() / p.z();
	T y = p.y() / p.z();

	x = intr.fx * x + intr.cx;
	y = intr.fy * y + intr.cy;

	return evec2<T>{ x, y };
}

template <typename T>
struct extrinsics
{
	equat<T> rotation;
	evec3<T> translation;
};

template <typename T>
static inline intrinsics<T> getIntrinsics(const T* const intr)
{
	intrinsics<T> result = { intr[0], intr[1], intr[2], intr[3] };
	return result;
}

template <typename T>
static inline extrinsics<T> getExtrinsics(const T* const trans, const T* const rot)
{
	extrinsics<T> result;
	result.rotation = equat<T>(
		  Eigen::AngleAxis<T>(rot[0], evec3<T>::UnitZ()).toRotationMatrix()  // Rot Z
		* Eigen::AngleAxis<T>(rot[1], evec3<T>::UnitY()).toRotationMatrix()  // Rot Y
		* Eigen::AngleAxis<T>(rot[2], evec3<T>::UnitX()).toRotationMatrix()); // Rot X

	result.translation = evec3<T>(trans[0], trans[1], trans[2]);
	return result;
}


template <typename cost_functor, int numResiduals, int N0, int N1 = 0, int N2 = 0, int N3 = 0, int N4 = 0, int N5 = 0, int N6 = 0, int N7 = 0, int N8 = 0, int N9 = 0>
struct autodiff_cost_function : public ceres::SizedCostFunction<numResiduals, N0, N1, N2, N3, N4, N5, N6, N7, N8, N9>
{
	autodiff_cost_function() {}

	explicit autodiff_cost_function(cost_functor* functor)
		: functor(functor) { }

	virtual bool Evaluate(double const* const* parameters,
		double* residuals,
		double** jacobians) const {
		if (!jacobians) {
			return ceres::internal::VariadicEvaluate<
				cost_functor, double, N0, N1, N2, N3, N4, N5, N6, N7, N8, N9>
				::Call(*functor, parameters, residuals);
		}
		return ceres::internal::AutoDiff<cost_functor, double,
			N0, N1, N2, N3, N4, N5, N6, N7, N8, N9>::Differentiate(
				*functor,
				parameters,
				ceres::SizedCostFunction<numResiduals,
				N0, N1, N2, N3, N4,
				N5, N6, N7, N8, N9>::num_residuals(),
				residuals,
				jacobians);
	}

	cost_functor* functor;
};

template <typename cost_functor, int numResiduals, int N0, int N1 = 0, int N2 = 0, int N3 = 0, int N4 = 0, int N5 = 0, int N6 = 0, int N7 = 0, int N8 = 0, int N9 = 0>
struct numericdiff_cost_function : public ceres::NumericDiffCostFunction<cost_functor, ceres::CENTRAL, numResiduals, N0, N1, N2, N3, N4, N5, N6, N7, N8, N9>
{
	numericdiff_cost_function(cost_functor* functor)
		: NumericDiffCostFunction(functor, ceres::DO_NOT_TAKE_OWNERSHIP) { }
	virtual ~numericdiff_cost_function() { }
};

struct backprojection_functor : autodiff_cost_function<backprojection_functor, 2, 4, 3, 3>
{
	evec3<double> camPos;
	evec2<double> observedProjPixel;


	backprojection_functor(evec3<double> camPos, evec2<double> projPixel)
		: autodiff_cost_function(this), camPos(camPos), observedProjPixel(projPixel) {}

	backprojection_functor(const backprojection_functor& o)
		: autodiff_cost_function(this), camPos(o.camPos), observedProjPixel(o.observedProjPixel) {}

	template <typename T>
	bool operator()(const T* const intr, const T* const trans, const T* const rot, T* residual) const
	{
		intrinsics<T> projIntr = getIntrinsics(intr);
		extrinsics<T> camToProj = getExtrinsics(trans, rot);

		evec3<T> projPos = camToProj.rotation * camPos.cast<T>() + camToProj.translation;

		evec2<T> projPixel = project(projPos, projIntr);

		evec2<T> error = projPixel - observedProjPixel.cast<T>();

		residual[0] = error.x();
		residual[1] = error.y();

		return true;
	}
};



void solveForCameraToProjectorParametersUsingCeres(const std::vector<calibration_solver_input>& input, 
	vec3& projPosition, quat& projRotation, camera_intrinsics& projIntrinsics, 
	calibration_solver_settings settings)
{
	Eigen::initParallel();

	ceres::Problem::Options problemOptions;
	problemOptions.cost_function_ownership = ceres::DO_NOT_TAKE_OWNERSHIP;
	problemOptions.loss_function_ownership = ceres::DO_NOT_TAKE_OWNERSHIP;
	problemOptions.local_parameterization_ownership = ceres::DO_NOT_TAKE_OWNERSHIP;

	ceres::Problem problem(problemOptions);


	quat R = conjugate(projRotation);
	vec3 t = -(R * projPosition);


	equat<double> eR = equat<double>(R.w, R.x, R.y, R.z);
	evec3<double> eulerAngles = eR.matrix().eulerAngles(2, 1, 0);

	double intr[] = { projIntrinsics.fx, projIntrinsics.fy, projIntrinsics.cx, projIntrinsics.cy };
	double trans[] = { t.x, t.y, t.z };
	double rot[] = { eulerAngles.x(), eulerAngles.y(), eulerAngles.z() };


	problem.AddParameterBlock(intr, 4, 0);
	problem.AddParameterBlock(trans, 3, 0);
	problem.AddParameterBlock(rot, 3, 0);

	std::vector<std::vector<backprojection_functor>> backprojResiduals(input.size());


	random_number_generator rng = { 61923 };

	uint32 index = 0;
	for (const calibration_solver_input& in : input)
	{
		backprojResiduals[index].reserve(in.renderedPC.numEntries);

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
						evec3<double> camPos = { e.position.x, e.position.y, e.position.z };
						evec2<double> projPixel = { proj.x, proj.y };

						auto& b = backprojResiduals[index].emplace_back(camPos, projPixel);
						problem.AddResidualBlock(&b, nullptr, intr, trans, rot);
					}
				}
			}
		}

		++index;
	}

	ceres::Solver::Options options;
	options.linear_solver_type = ceres::DENSE_NORMAL_CHOLESKY;
	options.num_threads = 8;
	options.minimizer_progress_to_stdout = true;
	options.max_num_iterations = (int)settings.maxNumIterations;
	options.evaluation_callback = 0;


	ceres::Solver::Summary summary;
	ceres::Solve(options, &problem, &summary);

	auto newProjIntrinsics = getIntrinsics(intr);
	auto newProjExtrinsics = getExtrinsics(trans, rot);

	{
		equat<float> eR = newProjExtrinsics.rotation.cast<float>();
		quat newRotation(eR.x(), eR.y(), eR.z(), eR.w());
		evec3<float> et = newProjExtrinsics.translation.cast<float>();
		vec3 newTranslation = { et.x(), et.y(), et.z() };

		projRotation = conjugate(newRotation);
		projPosition = -(projRotation * newTranslation);
		projIntrinsics = { (float)newProjIntrinsics.fx, (float)newProjIntrinsics.fy, (float)newProjIntrinsics.cx, (float)newProjIntrinsics.cy };
	}
}
