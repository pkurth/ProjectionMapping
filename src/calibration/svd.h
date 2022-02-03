#include "core/math.h"
#include "math_double.h"

struct svd3
{
	mat3d U;
	mat3d V;
	vec3d singularValues;
};

svd3 computeSVD(const mat3d& A);
