#include "pch.h"
#include "non_linear_least_squares.h"

struct sine_param_set
{
	float phase;
	float amplitude;
	float frequency;
};

struct lm_sine_test : least_squares_function<sine_param_set>
{
	static const uint32 PHASE = 0;
	static const uint32 AMP = 1;
	static const uint32 FREQ = 2;

	float value(const float* x, const sine_param_set& params) const override
	{
		return params.amplitude * sin(params.frequency * x[0] + params.phase);
	}

	void grad(const float* x, const sine_param_set& params, float* out) const override
	{
		out[FREQ] = params.amplitude * cos(params.frequency * x[0] + params.phase) * x[0];
		out[AMP] = sin(params.frequency * x[0] + params.phase);
		out[PHASE] = params.amplitude * cos(params.frequency * x[0] + params.phase);
	}
};

static least_squares_observation<1> createObservation(lm_sine_test test, sine_param_set params, float x)
{
	least_squares_observation<1> result;
	result.x[0] = x;
	result.y = test.value(&x, params);
	return result;
}

void testLevenbergMarquardt()
{
	lm_sine_test test;

	sine_param_set groundTruth = { 0.111f, 1.222f, 1.333f };
	least_squares_observation<1> observations[16];

	for (uint32 i = 0; i < arraysize(observations); ++i)
	{
		observations[i] = createObservation(test, groundTruth, i * 0.1f);
	}

	sine_param_set params = { 0.f, 1.f, 1.f };
	levenbergMarquardt(test, params, observations, arraysize(observations));

	int a = 0;
}
