#include "core/math.h"

template <typename param_set, uint32 numResiduals_ = 1>
struct least_squares_residual
{
	static const uint32 numParams = sizeof(param_set) / sizeof(float);
	static const uint32 numResiduals = numResiduals_;

	virtual void value(const param_set& params, float out[numResiduals]) const = 0;
	virtual void grad(const param_set& params, float out[numResiduals][numParams]) const = 0;
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

template <typename param_set, typename residual_t>
void gaussNewton(param_set& params, const residual_t* residuals, uint32 numResiduals,
	uint32 maxNumIterations = 20)
{
	const uint32 numSubResiduals = residual_t::numResiduals;
	const uint32 numParams = residual_t::numParams;

	for (uint32 gnIt = 0; gnIt < maxNumIterations; ++gnIt)
	{
		float JTJ[numParams][numParams] = {};
		float negJTr[numParams] = {};

		for (uint32 i = 0; i < numResiduals; ++i)
		{
			float grad[numSubResiduals][numParams];
			residuals[i].grad(params, grad);

			float value[numSubResiduals];
			residuals[i].value(params, value);

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
static float chiSquared(param_set& params, const residual_t* residuals, uint32 numResiduals)
{
	const uint32 numSubResiduals = residual_t::numResiduals;

	float sum = 0.f;

	for (uint32 i = 0; i < numResiduals; ++i)
	{
		float value[numSubResiduals];
		residuals[i].value(params, value);
		for (uint32 s = 0; s < numSubResiduals; ++s)
		{
			float d = value[s];
			//d = d / s[i];
			sum = sum + (d * d);
		}
	}

	return sum;
}

template <typename param_set, typename residual_t>
void levenbergMarquardt(param_set& params, const residual_t* residuals, uint32 numResiduals,
	float lambda = 0.001f, float termEpsilon = 0.01f, uint32 maxNumIterations = 20)
{
	// http://scribblethink.org/Computer/Javanumeric/LM.java

	const uint32 numSubResiduals = residual_t::numResiduals;
	const uint32 numParams = residual_t::numParams;

	float e0 = chiSquared( params, residuals, numResiduals);

	uint32 term = 0;

	for (uint32 lmIt = 0; lmIt < maxNumIterations; ++lmIt)
	{
		float H[numParams][numParams] = {};
		float g[numParams] = {};

		// Hessian approximation and gradient.
		for (uint32 i = 0; i < numResiduals; ++i)
		{
			float grad[numSubResiduals][numParams];
			residuals[i].grad(params, grad);

			float value[numSubResiduals];
			residuals[i].value(params, value);

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

		// Boost diagonal towards gradient descent.
		for (uint32 r = 0; r < numParams; ++r)
		{
			H[r][r] *= (1.f + lambda);
		}

		float x[numParams] = {};
		conjugateGradient(H, g, x, 5);

		param_set newParams = params;
		for (uint32 i = 0; i < numParams; ++i)
		{
			float& n = ((float*)&newParams)[i];
			n += x[i];
		}


		float e1 = chiSquared(newParams, residuals, numResiduals);

		bool done = false;

		// Termination test (slightly different than NR)
		if (abs(e1 - e0) > termEpsilon)
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
			lambda *= 10.f;
		}
		else
		{
			// New location is better, accept new parameters.
			lambda *= 0.1f;
			e0 = e1;
			params = newParams;
		}

		if (done)
		{
			break;
		}
	}
}

