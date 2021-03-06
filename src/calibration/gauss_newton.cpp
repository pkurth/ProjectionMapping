#include "pch.h"
#include "non_linear_least_squares.h"


struct gn_param_set
{
	double b0, b1;
};

struct gn_test_residual : least_squares_residual<gn_param_set>
{
	double x;
	double y;

	void value(const gn_param_set& params, double out[1]) const
	{
		double v = params.b0 * x / (params.b1 + x);
		out[0] = y - v;
	}

	void grad(const gn_param_set& params, double out[1][2]) const
	{
		out[0][0] = -x / (params.b1 + x);
		out[0][1] = (params.b0 * x) / ((params.b1 + x) * (params.b1 + x));
	}
};

static gn_test_residual createResidual(double x, double y)
{
	gn_test_residual result;
	result.x = x;
	result.y = y;
	return result;
}

void testGaussNewton()
{
	gn_param_set groundTruth = { 0.362f, 0.556f };

	gn_test_residual residuals[] =
	{
		createResidual(0.038f, 0.05f),
		createResidual(0.194f, 0.127f),
		createResidual(0.425f, 0.094f),
		createResidual(0.626f, 0.2122f),
		createResidual(1.253f, 0.2729f),
		createResidual(2.5f, 0.2665f),
		createResidual(3.74f, 0.3317f),
	};

	gauss_newton_settings settings;

	gn_param_set params = { 0.9f, 0.2f };
	gaussNewton(settings, params, residuals, arraysize(residuals));

	int a = 0;
}
