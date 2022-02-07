#include "core/math.h"


struct default_precompute_t
{
	template <typename T>
	void precompute(const T&) {}
};

template <typename param_set_, uint32 numResiduals_ = 1, typename precompute_t_ = default_precompute_t>
struct least_squares_residual
{
	using param_set = param_set_;
	using precompute_t = precompute_t_;
	static const uint32 numParams = sizeof(param_set) / sizeof(double);
	static const uint32 numResiduals = numResiduals_;

	/*
	* A residual must provide two functions 'value' and 'grad'.
	* - void value(const param_set& params, double out[numResiduals]) const
	* - void grad(const param_set& params, double out[numResiduals][numParams]) const
	* 
	* Optionally, you can pass a 'precompute_t' type, in which case your functions can take a third argument of type 'precompute_t':
	* - void value(const param_set& params, const precompute_t& precompute, double out[numResiduals]) const
	* - void grad(const param_set& params, const precompute_t& precompute, double out[numResiduals][numParams]) const
	* 
	* The type 'precompute_t' must implement a function 'precompute(const param_set&)', which is called from the solver before each iteration. 
	* You can use this to compute some common data.
	*/
};

template <typename residual_t, typename param_set = residual_t::param_set, uint32 numResiduals = residual_t::numResiduals>
struct least_squares_residual_array
{
	const residual_t* residuals;
	uint32 count;

	least_squares_residual_array(const residual_t* residuals, uint32 numResiduals) : residuals(residuals), count(numResiduals) {}
	least_squares_residual_array(const std::vector<residual_t>& residuals) : residuals(residuals.data()), count((uint32)residuals.size()) {}
	template <uint32 N> least_squares_residual_array(const residual_t(&residuals)[N]) : residuals(residuals), count(N) {}
};

struct gauss_newton_settings
{
	uint32 maxNumIterations = 20;
	uint32 numCGIterations = 10;
};

struct levenberg_marquardt_settings
{
	uint32 maxNumIterations = 20;
	double lambda = 0.001f; 
	double termEpsilon = 0.01f;
	uint32 numCGIterations = 10;
};

struct levenberg_marquardt_result
{
	uint32 numIterations;
	double epsilon;
};




static double dot(double* a, double* b, uint32 N)
{
	double result = 0.f;
	for (uint32 i = 0; i < N; ++i)
	{
		result += a[i] * b[i];
	}
	return result;
}

template <uint32 N>
static void conjugateGradient(const double (&A)[N][N], const double (&b)[N], double (&x)[N], uint32 numIterations)
{
	memset(x, 0, sizeof(x));

	double r[N], p[N];
	memcpy(r, b, sizeof(b));
	memcpy(p, b, sizeof(b));

	double rdotr = dot(r, r, N);

	for (uint32 cgIt = 0; cgIt < numIterations; ++cgIt)
	{
		double Ap[N];

		for (uint32 y = 0; y < N; ++y)
		{
			Ap[y] = 0.f;
			for (uint32 i = 0; i < N; ++i)
			{
				Ap[y] += A[y][i] * p[i];
			}
		}

		double pAp = dot(p, Ap, N);

		double alpha = rdotr / pAp;
		for (uint32 i = 0; i < N; ++i)
		{
			x[i] += p[i] * alpha;
			r[i] -= Ap[i] * alpha;
		}

		double oldrdotr = rdotr;
		rdotr = dot(r, r, N);
		if (rdotr < 1e-7f)
		{
			break;
		}

		double beta = rdotr / oldrdotr;
		for (uint32 i = 0; i < N; ++i)
		{
			p[i] = r[i] + p[i] * beta;
		}
	}
}


template <typename param_set, typename residual_t, uint32 numSubResiduals, uint32 numParams>
static void gaussNewtonInternal(param_set& params, least_squares_residual_array<residual_t, param_set, numSubResiduals> residualArray,
	double(&JTJ)[numParams][numParams], double(&negJTr)[numParams])
{
	residual_t::precompute_t precompute;
	precompute.precompute(params);

	for (uint32 i = 0; i < residualArray.count; ++i)
	{
		double grad[numSubResiduals][numParams];
		double value[numSubResiduals];

		if constexpr (std::is_same_v<default_precompute_t, residual_t::precompute_t>)
		{
			residualArray.residuals[i].grad(params, grad);
			residualArray.residuals[i].value(params, value);
		}
		else
		{
			residualArray.residuals[i].grad(params, precompute, grad);
			residualArray.residuals[i].value(params, precompute, value);
		}

		for (uint32 s = 0; s < numSubResiduals; ++s)
		{
			for (uint32 r = 0; r < numParams; ++r)
			{
				for (uint32 c = 0; c < numParams; ++c)
				{
					JTJ[r][c] += grad[s][r] * grad[s][c];
				}

				negJTr[r] += -grad[s][r] * value[s];
			}
		}
	}
}

template <typename param_set, typename... residual_t, uint32... numSubResiduals>
void gaussNewton(gauss_newton_settings settings, param_set& params, 
	least_squares_residual_array<residual_t, param_set, numSubResiduals>... residualArrays)
{
	const uint32 numParams = sizeof(param_set) / sizeof(double);

	for (uint32 gnIt = 0; gnIt < settings.maxNumIterations; ++gnIt)
	{
		double JTJ[numParams][numParams] = {};
		double negJTr[numParams] = {};

		(gaussNewtonInternal(params, residualArrays, JTJ, negJTr), ...);

		double x[numParams] = {}; // Step
		conjugateGradient(JTJ, negJTr, x, settings.numCGIterations);

		for (uint32 i = 0; i < numParams; ++i)
		{
			double& n = ((double*)&params)[i];
			n += x[i];
		}
	}
}

template <typename param_set, typename residual_t>
void gaussNewton(gauss_newton_settings settings, param_set& params, const residual_t* residuals, uint32 numResiduals)
{
	least_squares_residual_array<residual_t> arr(residuals, numResiduals);
	gaussNewton(settings, params, arr);
}

template <typename param_set, typename residual_t, uint32 numSubResiduals>
static double chiSquared(param_set& params, least_squares_residual_array<residual_t, param_set, numSubResiduals> residualArray)
{
	residual_t::precompute_t precompute;
	precompute.precompute(params);

	double sum = 0.f;

	for (uint32 i = 0; i < residualArray.count; ++i)
	{
		double value[numSubResiduals];

		if constexpr (std::is_same_v<default_precompute_t, residual_t::precompute_t>)
		{
			residualArray.residuals[i].value(params, value);
		}
		else
		{
			residualArray.residuals[i].value(params, precompute, value);
		}

		for (uint32 s = 0; s < numSubResiduals; ++s)
		{
			double d = value[s];
			sum += d * d;
		}
	}

	return sum;
}

template <typename param_set, typename residual_t, uint32 numSubResiduals, uint32 numParams>
static void levenbergMarquardtInternal(const param_set& params, least_squares_residual_array<residual_t, param_set, numSubResiduals> residualArray,
	double(&H)[numParams][numParams], double(&g)[numParams])
{
	residual_t::precompute_t precompute;
	precompute.precompute(params);

	for (uint32 i = 0; i < residualArray.count; ++i)
	{
		double grad[numSubResiduals][numParams];
		double value[numSubResiduals];
		
		if constexpr (std::is_same_v<default_precompute_t, residual_t::precompute_t>)
		{
			residualArray.residuals[i].grad(params, grad);
			residualArray.residuals[i].value(params, value);
		}
		else
		{
			residualArray.residuals[i].grad(params, precompute, grad);
			residualArray.residuals[i].value(params, precompute, value);
		}

		double oos2 = 1.f; // Squared observation weight.

		for (uint32 s = 0; s < numSubResiduals; ++s)
		{
			for (uint32 r = 0; r < numParams; ++r)
			{
				for (uint32 c = 0; c < numParams; ++c)
				{
					H[r][c] += oos2 * grad[s][r] * grad[s][c];
				}

				g[r] += oos2 * value[s] * grad[s][r];
			}
		}
	}
}

template <typename param_set, typename... residual_t, uint32... numSubResiduals>
levenberg_marquardt_result levenbergMarquardt(levenberg_marquardt_settings settings, param_set& params,
	least_squares_residual_array<residual_t, param_set, numSubResiduals>... residualArrays)
{
	// http://scribblethink.org/Computer/Javanumeric/LM.java

	const uint32 numParams = sizeof(param_set) / sizeof(double);

	double e0 = (chiSquared(params, residualArrays) + ...);

	uint32 term = 0;

	levenberg_marquardt_result result = {};

	for (uint32 lmIt = 0; lmIt < settings.maxNumIterations; ++lmIt)
	{
		++result.numIterations;

		double H[numParams][numParams] = {};
		double g[numParams] = {};

		// Hessian approximation and gradient.
		(levenbergMarquardtInternal(params, residualArrays, H, g), ...);

		// Boost diagonal towards gradient descent.
		for (uint32 r = 0; r < numParams; ++r)
		{
			H[r][r] *= (1.f + settings.lambda);
		}

		double x[numParams] = {};
		conjugateGradient(H, g, x, settings.numCGIterations);

		param_set newParams = params;
		for (uint32 i = 0; i < numParams; ++i)
		{
			double& n = ((double*)&newParams)[i];
			n += x[i];
		}


		double e1 = (chiSquared(params, residualArrays) + ...);
		result.epsilon = abs(e1 - e0);

		bool done = false;

		// Termination test.
		if (abs(e1 - e0) > settings.termEpsilon)
		{
			term = 0;
		}
		else
		{
			++term;
			if (term == 4)
			{
				done = true;
			}
		}

		if (e1 > e0 || isnan(e1))
		{
			// New location is worse than before.
			settings.lambda *= 10.f;
		}
		else
		{
			// New location is better, accept new parameters.
			settings.lambda *= 0.1f;
			e0 = e1;
			params = newParams;
		}

		if (done)
		{
			break;
		}
	}

	return result;
}

template <typename param_set, typename residual_t>
levenberg_marquardt_result levenbergMarquardt(levenberg_marquardt_settings settings, param_set& params, 
	const residual_t* residuals, uint32 numResiduals)
{
	least_squares_residual_array<residual_t> arr(residuals, numResiduals);
	return levenbergMarquardt(settings, params, arr);
}

