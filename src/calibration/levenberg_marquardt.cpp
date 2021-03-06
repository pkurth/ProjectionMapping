#include "pch.h"
#include "non_linear_least_squares.h"

struct sine_param_set
{
	double phase;
	double amplitude;
	double frequency;
};

static double evaluate(const sine_param_set& params, double x)
{
	return params.amplitude * sin(params.frequency * x + params.phase);
}

struct lm_sine_test : least_squares_residual<sine_param_set>
{
	double x;
	double y;

	void value(const sine_param_set& params, double out[1]) const
	{
		double v = evaluate(params, x);
		out[0] = y - v;
	}

	void grad(const sine_param_set& params, double out[1][3]) const
	{
		out[0][0] = params.amplitude * cos(params.frequency * x + params.phase);
		out[0][1] = sin(params.frequency * x + params.phase);
		out[0][2] = params.amplitude * cos(params.frequency * x + params.phase) * x;
	}
};

static lm_sine_test createResidual(sine_param_set params, double x)
{
	lm_sine_test result;
	result.x = x;
	result.y = evaluate(params, x);
	return result;
}

void testLevenbergMarquardt()
{
	sine_param_set groundTruth = { 0.111f, 1.222f, 1.333f };
	lm_sine_test residuals[16];

	for (uint32 i = 0; i < arraysize(residuals); ++i)
	{
		residuals[i] = createResidual(groundTruth, i * 0.1f);
	}

	levenberg_marquardt_settings settings;

	sine_param_set params = { 0.f, 1.f, 1.f };
	levenbergMarquardt(settings, params, residuals, arraysize(residuals));

	int a = 0;
}
