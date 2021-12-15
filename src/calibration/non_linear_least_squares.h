#include "core/math.h"

template <typename param_set_, uint32 numResiduals_ = 1>
struct least_squares_residual
{
	using param_set = param_set_;
	static const uint32 numParams = sizeof(param_set) / sizeof(float);
	static const uint32 numResiduals = numResiduals_;

	virtual void value(const param_set& params, float out[numResiduals]) const = 0;
	virtual void grad(const param_set& params, float out[numResiduals][numParams]) const = 0;
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
};

struct levenberg_marquardt_settings
{
	uint32 maxNumIterations = 20;
	float lambda = 0.001f; 
	float termEpsilon = 0.01f;
};




static float dot(float* a, float* b, uint32 N)
{
	float result = 0.f;
	for (uint32 i = 0; i < N; ++i)
	{
		result += a[i] * b[i];
	}
	return result;
}

template <uint32 N>
static void conjugateGradient(const float (&A)[N][N], const float (&b)[N], float (&x)[N], uint32 numIterations)
{
	memset(x, 0, sizeof(x));

	float r[N], p[N];
	memcpy(r, b, sizeof(b));
	memcpy(p, b, sizeof(b));

	float rdotr = dot(r, r, N);

	for (uint32 cgIt = 0; cgIt < numIterations; ++cgIt)
	{
		float Ap[N];

		for (uint32 y = 0; y < N; ++y)
		{
			Ap[y] = 0.f;
			for (uint32 i = 0; i < N; ++i)
			{
				Ap[y] += A[y][i] * p[i];
			}
		}

		float pAp = dot(p, Ap, N);

		float alpha = rdotr / pAp;
		for (uint32 i = 0; i < N; ++i)
		{
			x[i] += p[i] * alpha;
			r[i] -= Ap[i] * alpha;
		}

		float oldrdotr = rdotr;
		rdotr = dot(r, r, N);
		if (rdotr < 1e-7f)
		{
			break;
		}

		float beta = rdotr / oldrdotr;
		for (uint32 i = 0; i < N; ++i)
		{
			p[i] = r[i] + p[i] * beta;
		}
	}
}


template <typename param_set, typename residual_t, uint32 numSubResiduals, uint32 numParams>
static void gaussNewtonInternal(param_set& params, least_squares_residual_array<residual_t, param_set, numSubResiduals> residualArray,
	float(&JTJ)[numParams][numParams], float(&negJTr)[numParams])
{
	for (uint32 i = 0; i < residualArray.count; ++i)
	{
		float grad[numSubResiduals][numParams];
		residualArray.residuals[i].grad(params, grad);

		float value[numSubResiduals];
		residualArray.residuals[i].value(params, value);

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
	const uint32 numParams = sizeof(param_set) / sizeof(float);

	for (uint32 gnIt = 0; gnIt < settings.maxNumIterations; ++gnIt)
	{
		float JTJ[numParams][numParams] = {};
		float negJTr[numParams] = {};

		(gaussNewtonInternal(params, residualArrays, JTJ, negJTr), ...);

		float x[numParams] = {}; // Step
		conjugateGradient(JTJ, negJTr, x, 5);

		for (uint32 i = 0; i < numParams; ++i)
		{
			float& n = ((float*)&params)[i];
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
static float chiSquaredInternal(param_set& params, least_squares_residual_array<residual_t, param_set, numSubResiduals> residualArray)
{
	float sum = 0.f;

	for (uint32 i = 0; i < residualArray.count; ++i)
	{
		float value[numSubResiduals];
		residualArray.residuals[i].value(params, value);
		for (uint32 s = 0; s < numSubResiduals; ++s)
		{
			float d = value[s];
			//d = d / s[i];
			sum = sum + (d * d);
		}
	}

	return sum;
}

template <typename param_set, typename... residual_t, uint32... numSubResiduals>
static float chiSquared(param_set& params, least_squares_residual_array<residual_t, param_set, numSubResiduals>... residualArray)
{
	return (chiSquaredInternal(params, residualArray) + ...);
}

template <typename param_set, typename residual_t, uint32 numSubResiduals, uint32 numParams>
static void levenbergMarquardtInternal(param_set& params, least_squares_residual_array<residual_t, param_set, numSubResiduals> residualArray,
	float(&H)[numParams][numParams], float(&g)[numParams])
{
	for (uint32 i = 0; i < residualArray.count; ++i)
	{
		float grad[numSubResiduals][numParams];
		residualArray.residuals[i].grad(params, grad);

		float value[numSubResiduals];
		residualArray.residuals[i].value(params, value);

		float oos2 = 1.f; // Squared observation weight.

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
void levenbergMarquardt(levenberg_marquardt_settings settings, param_set& params, 
	least_squares_residual_array<residual_t, param_set, numSubResiduals>... residualArrays)
{
	// http://scribblethink.org/Computer/Javanumeric/LM.java

	const uint32 numParams = sizeof(param_set) / sizeof(float);

	float e0 = chiSquared(params, residualArrays...);

	uint32 term = 0;

	for (uint32 lmIt = 0; lmIt < settings.maxNumIterations; ++lmIt)
	{
		float H[numParams][numParams] = {};
		float g[numParams] = {};

		// Hessian approximation and gradient.
		(levenbergMarquardtInternal(params, residualArrays, H, g), ...);

		// Boost diagonal towards gradient descent.
		for (uint32 r = 0; r < numParams; ++r)
		{
			H[r][r] *= (1.f + settings.lambda);
		}

		float x[numParams] = {};
		conjugateGradient(H, g, x, 5);

		param_set newParams = params;
		for (uint32 i = 0; i < numParams; ++i)
		{
			float& n = ((float*)&newParams)[i];
			n += x[i];
		}


		float e1 = chiSquared(newParams, residualArrays...);

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
}

template <typename param_set, typename residual_t>
void levenbergMarquardt(levenberg_marquardt_settings settings, param_set& params, const residual_t* residuals, uint32 numResiduals)
{
	least_squares_residual_array<residual_t> arr(residuals, numResiduals);
	levenbergMarquardt(settings, params, arr);
}

