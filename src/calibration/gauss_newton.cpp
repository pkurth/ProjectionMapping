#include "pch.h"
#include "non_linear_least_squares.h"


struct gn_param_set
{
	float b0, b1;
};

struct gn_test_residual : least_squares_residual<gn_param_set>
{
	float x;
	float y;

	void value(const gn_param_set& params, float out[1]) const override
	{
		float v = params.b0 * x / (params.b1 + x);
		out[0] = y - v;
	}

	void grad(const gn_param_set& params, float out[1][2]) const override
	{
		out[0][0] = -x / (params.b1 + x);
		out[0][1] = (params.b0 * x) / ((params.b1 + x) * (params.b1 + x));
	}
};

static gn_test_residual createResidual(float x, float y)
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

	gn_param_set params = { 0.9f, 0.2f };
	gaussNewton(params, residuals, arraysize(residuals));

	int a = 0;
}
