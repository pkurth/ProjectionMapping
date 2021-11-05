#pragma once

#include "math.h"

struct random_number_generator
{
	// XOR shift generator.

	uint32 state;

	inline uint32 randomUint()
	{
		uint32 x = state;
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		state = x;
		return x;
	}

	inline uint32 randomUintBetween(uint32 lo, uint32 hi)
	{
		return randomUint() % (hi - lo) + lo;
	}

	inline float randomFloat01()
	{
		return randomUint() / (float)UINT_MAX;
	}

	inline float randomFloatBetween(float lo, float hi)
	{
		return remap(randomFloat01(), 0.f, 1.f, lo, hi);
	}

	inline vec2 randomVec2Between(float lo, float hi)
	{
		return vec2(
			randomFloatBetween(lo, hi),
			randomFloatBetween(lo, hi));
	}

	inline vec3 randomVec3Between(float lo, float hi)
	{
		return vec3(
			randomFloatBetween(lo, hi),
			randomFloatBetween(lo, hi),
			randomFloatBetween(lo, hi));
	}

	inline vec4 randomVec4Between(float lo, float hi)
	{
		return vec4(
			randomFloatBetween(lo, hi),
			randomFloatBetween(lo, hi),
			randomFloatBetween(lo, hi),
			randomFloatBetween(lo, hi));
	}

	inline vec3 randomPointOnUnitSphere()
	{
		return normalize(randomVec3Between(-1.f, 1.f));
	}

	inline quat randomRotation(float maxAngle = M_PI)
	{
		return quat(randomPointOnUnitSphere(), randomFloatBetween(-maxAngle, maxAngle));
	}
};

static float halton(uint32 index, uint32 base)
{
	float fraction = 1.f;
	float result = 0.f;
	while (index > 0)
	{
		fraction /= (float)base;
		result += fraction * (index % base);
		index = ~~(index / base);
	}
	return result;
}

static vec2 halton23(uint32 index)
{
	return vec2(halton(index, 2), halton(index, 3));
}

