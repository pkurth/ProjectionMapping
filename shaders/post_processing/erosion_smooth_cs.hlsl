#define OP min
#define NULL_VALUE 1.f

static float map(float f, int distance, float radius)
{
	return f + distance / radius;
}

#include "morphology_smooth_common.hlsli"

