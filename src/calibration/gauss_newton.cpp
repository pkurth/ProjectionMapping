#include "pch.h"
#include "non_linear_least_squares.h"


struct gn_param_set
{
	float b0, b1;
};

struct gn_test : least_squares_function<gn_param_set>
{
	static const uint32 B0 = 0;
	static const uint32 B1 = 1;

	float value(const float* x, const gn_param_set& params) const override
	{
		return params.b0 * x[0] / (params.b1 + x[0]);
	}

	void grad(const float* x, const gn_param_set& params, float* out) const override
	{
		out[B0] = -x[0] / (params.b1 + x[0]);
		out[B1] = (params.b0 * x[0]) / ((params.b1 + x[0]) * (params.b1 + x[0]));
	}
};

static least_squares_observation<1> createObservation(float x, float y)
{
	least_squares_observation<1> result;
	result.x[0] = x;
	result.y = y;
	return result;
}

void testGaussNewton()
{
	gn_test test;

	gn_param_set groundTruth = { 0.362f, 0.556f };
	least_squares_observation<1> observations[] = 
	{
		createObservation(0.038f, 0.05f),
		createObservation(0.194f, 0.127f),
		createObservation(0.425f, 0.094f),
		createObservation(0.626f, 0.2122f),
		createObservation(1.253f, 0.2729f),
		createObservation(2.5f, 0.2665f),
		createObservation(3.74f, 0.3317f),
	};

	gn_param_set params = { 0.9f, 0.2f };
	gaussNewton(test, params, observations, arraysize(observations));

	int a = 0;
}
