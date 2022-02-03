#include "pch.h"
#include "fundamental.h"
#include "calibration_internal.h"
#include "svd.h"
#include "core/random.h"



struct mat9
{
	float m[9 * 9]; // Column major.

	float& operator()(uint32 y, uint32 x) { return m[x * 9 + y]; }
	const float& operator()(uint32 y, uint32 x) const { return m[x * 9 + y]; }
};

struct vec9
{
	float m[9];
};

struct qr_decomposition
{
	mat9 Q, R;
};

static void computeMinor(mat9& m, const mat9& from, uint32 d)
{
	for (uint32 y = 0; y < 9; ++y)
	{
		for (uint32 x = 0; x < 9; ++x)
		{
			if (x < d || y < d)
			{
				m(y, x) = (x == y) ? 1.f : 0.f;
			}
			else
			{
				m(y, x) = from(y, x);
			}
		}
	}
}

static float length(const vec9& v)
{
	float result = 0.f;
	for (uint32 i = 0; i < 9; ++i)
	{
		result += v.m[i] * v.m[i];
	}
	return sqrt(result);
}

static vec9 extractColumn(const mat9& m, uint32 c)
{
	vec9 result;
	memcpy(result.m, &m.m[c * 9], sizeof(float) * 9);
	return result;
}

static vec9 scaledAdd(const vec9& a, const vec9& b, float s)
{
	vec9 result;
	for (uint32 i = 0; i < 9; ++i)
	{
		result.m[i] = a.m[i] + s * b.m[i];
	}
	return result;
}

static vec9 normalize(const vec9& v)
{
	vec9 result;
	float l = length(v);
	float f = (l < EPSILON) ? 1.f : 1.f / length(v);
	for (uint32 i = 0; i < 9; ++i)
	{
		result.m[i] = v.m[i] * f;
	}
	return result;
}

static void computeHouseholderFactor(mat9& mat, const vec9& v)
{
	for (uint32 i = 0; i < 9; ++i)
	{
		for (uint32 j = 0; j < 9; ++j)
		{
			mat(i, j) = -2.f * v.m[i] * v.m[j];
		}
	}
	for (uint32 i = 0; i < 9; ++i)
	{
		mat(i, i) += 1.f;
	}
}

static void multiply(const mat9& a, const mat9& b, mat9& result)
{
	for (uint32 i = 0; i < 9; ++i)
	{
		for (uint32 j = 0; j < 9; ++j)
		{
			float v = 0.f;
			for (uint32 k = 0; k < 9; ++k)
			{
				v += a(i, k) * b(k, j);
			}
			result(i, j) = v;
		}
	}
}

static mat9 transpose(const mat9& m)
{
	mat9 result;
	for (uint32 y = 0; y < 9; ++y)
	{
		for (uint32 x = 0; x < 9; ++x)
		{
			result(y, x) = m(x, y);
		}
	}
	return result;
}

static bool isDiagonal(const mat9& m, float threshold = 1e-5f)
{
	for (uint32 y = 0, idx = 0; y < 9; ++y)
	{
		for (uint32 x = 0; x < 9; ++x, ++idx)
		{
			if (x != y)
			{
				if (!fuzzyEquals(m.m[idx], 0.f, threshold))
				{
					return false;
				}
			}
		}
	}
	return true;
}

static vec9 diagonal(const mat9& m)
{
	vec9 result;
	for (uint32 i = 0; i < 9; ++i)
	{
		result.m[i] = m(i, i);
	}
	return result;
}

static vec9 operator-(const vec9& a, const vec9& b)
{
	vec9 result;
	for (uint32 i = 0; i < 9; ++i)
	{
		result.m[i] = a.m[i] - b.m[i];
	}
	return result;
}

static float dot(const vec9& a, const vec9& b)
{
	float result = 0;
	for (uint32 i = 0; i < 9; ++i)
	{
		result += a.m[i] * b.m[i];
	}
	return result;
}

qr_decomposition qrDecomposition(const mat9& mat)
{
	mat9 qv[8];

	// Temp array.
	mat9 z = mat;
	mat9 z1;

	for (uint32 k = 0; k < 8; ++k)
	{
		computeMinor(z1, z, k);
		vec9 x = extractColumn(z1, k);

		float a = length(x);
		if (mat(k, k) > 0.f)
		{
			a = -a;
		}

		vec9 e;
		for (uint32 i = 0; i < 9; ++i)
		{
			e.m[i] = (i == k) ? 1.f : 0.f;
		}

		// e = x + a*e
		e = scaledAdd(x, e, a);
		e = normalize(e);

		// qv[k] = I - 2 *e*e^T
		computeHouseholderFactor(qv[k], e);

		// z = qv[k] * z1
		multiply(qv[k], z1, z);

	}

	qr_decomposition result;
	result.Q = qv[0];

	// After this loop, we will obtain Q (up to a transpose operation)
	for (uint32 i = 1; i < 8; ++i)
	{
		multiply(qv[i], result.Q, z1);
		result.Q = z1;
	}

	multiply(result.Q, mat, result.R);
	result.Q = transpose(result.Q);

	return result;
}

static bool getEigen(mat9 A, vec9& outEigenValues, mat9& outEigenVectors)
{
	mat9 Q = qrDecomposition(A).Q;
	mat9 Qt = transpose(Q);

	mat9 E, tmp;
	multiply(Qt, A, tmp);
	multiply(tmp, Q, E);

	mat9 U = Q;

	vec9 res = diagonal(E);
	vec9 init = diagonal(A);

	vec9 error = init - res;
	float e = dot(error, error);

	while (e > 0.001f)
	{
		init = res;

		mat9 Q = qrDecomposition(E).Q;
		mat9 Qt = transpose(Q);

		multiply(Qt, E, tmp);
		multiply(tmp, Q, E);

		multiply(U, Q, tmp);
		U = tmp;

		res = diagonal(E);
		vec9 error = init - res;
		e = dot(error, error);
	}

	outEigenValues = res;
	outEigenVectors = U;

	return true;
}

static mat3 eightPointAlgorithm(const std::vector<pixel_correspondence>& pc)
{
	int count = (int)pc.size();
	float t = 1.f / count;

	vec2 m1c(0, 0), m2c(0, 0);
	float scale1 = 0, scale2 = 0;

	for (int i = 0; i < count; ++i)
	{
		m1c += pc[i].camera;
		m2c += pc[i].projector;
	}
	m1c *= t;
	m2c *= t;

	for (int i = 0; i < count; ++i)
	{
		scale1 += length(pc[i].camera - m1c);
		scale2 += length(pc[i].projector - m2c);
	}
	scale1 *= t;
	scale2 *= t;

	if (scale1 < FLT_EPSILON || scale2 < FLT_EPSILON)
	{
		return mat3::identity;
	}

	scale1 = sqrt(2.f) / scale1;
	scale2 = sqrt(2.f) / scale2;

	mat9 A = {};

	// Form a linear system Ax=0: for each selected pair of points m1 & m2,
	// the row of A(=a) represents the coefficients of equation: (m2, 1)'*F*(m1, 1) = 0.
	// To save computation time, we compute (At*A) instead of A and then solve (At*A)x=0.
	for (int i = 0; i < count; ++i)
	{
		float x1 = (pc[i].camera.x - m1c.x) * scale1;
		float y1 = (pc[i].camera.y - m1c.y) * scale1;
		float x2 = (pc[i].projector.x - m2c.x) * scale2;
		float y2 = (pc[i].projector.y - m2c.y) * scale2;
		vec9 r = { x2 * x1, x2 * y1, x2, y2 * x1, y2 * y1, y2, x1, y1, 1.f };

		// A += r * rT.
		for (uint32 y = 0; y < 9; ++y)
		{
			for (uint32 x = 0; x < 9; ++x)
			{
				A(y, x) += r.m[y] * r.m[x];
			}
		}
	}

#if 0
	qr_decomposition qr = qrDecomposition(A);

	mat9 test;
	multiply(qr.Q, qr.R, test);

	for (int i = 0; i < 81; ++i)
	{
		assert(fuzzyEquals(test.m[i], A.m[i]));
	}
#endif

	vec9 W; // Eigen values.
	mat9 V; // Eigen vectors.
	getEigen(A, W, V);


	int i;
	for (i = 0; i < 9; ++i)
	{
		if (fabs(W.m[i]) < EPSILON)
		{
			break;
		}
	}

	if (i < 8)
	{
		return mat3::identity;
	}

	mat3 F0;
	memcpy(F0.m, &V(0, 8), sizeof(float) * 9); // Last column: Eigen vector corresponding to smallest Eigen value.

	svd3 svd = computeSVD(F0);

	vec3 w = svd.singularValues;
	w.z = 0.f;

	F0 = svd.U * mat3(w.x, 0.f, 0.f, 0.f, w.y, 0.f, 0.f, 0.f, w.z) * transpose(svd.V);

	// Apply the transformation that is inverse to what we used to normalize the point coordinates.
	mat3 T1(scale1, 0, -scale1 * m1c.x, 0, scale1, -scale1 * m1c.y, 0.f, 0.f, 1.f);
	mat3 T2(scale2, 0, -scale2 * m2c.x, 0, scale2, -scale2 * m2c.y, 0.f, 0.f, 1.f);

	F0 = transpose(T2) * F0 * T1;

	// Make F(3,3) = 1
	if (fabs(F0.m22) > FLT_EPSILON)
	{
		F0 *= 1.f / F0.m22;
	}

	return F0;
}

void testQR()
{
	std::vector<pixel_correspondence> pcs =
	{
		{ { 496, 541 }, {794, 1030} },
		{ { 722, 494 }, {748, 741} },
		{ { 363, 400 }, {650, 1185} },
		{ { 778, 481 }, {729, 665} },
		{ { 1056, 562 }, {811, 273} },
		{ { 1023, 530 }, {770, 320} },
		{ { 450, 405 }, {584, 1103} },
		{ { 517, 479 }, {733, 1001} },
	};

	eightPointAlgorithm(pcs);
}

static int ransacUpdateNumIters(float p, float ep, int modelPoints, int maxIters)
{
	p = max(p, 0.f);
	p = min(p, 1.f);
	ep = max(ep, 0.f);
	ep = min(ep, 1.f);

	// Avoid infs & nans.
	float num = max(1.f - p, FLT_MIN);
	float denom = 1.f - pow(1.f - ep, (float)modelPoints);
	if (denom < FLT_MIN)
	{
		return 0;
	}

	num = log(num);
	denom = log(denom);

	return (denom >= 0 || -num >= maxIters * (-denom)) ? maxIters : (int)std::round(num / denom);
}

mat3 computeFundamentalMatrix(const std::vector<pixel_correspondence>& pc, std::vector<uint8>& outMask)
{
	int numIterations = 1000;
	float confidence = 0.99f;
	float tolerance = 1.f;

	int count = (int)pc.size();
	outMask.resize(count);
	std::fill(outMask.begin(), outMask.end(), 0);

	if (count < 8)
	{
		return mat3::identity;
	}

	if (count == 8)
	{
		std::fill(outMask.begin(), outMask.end(), 1);
		return eightPointAlgorithm(pc);
	}

	std::vector<pixel_correspondence> pcCopy = pc; // For shuffling.
	std::vector<pixel_correspondence> subset(8);

	std::vector<uint8> tmpMask(count);
	int bestNumInliers = 0;

	random_number_generator rng = { 1234 };

	for (int it = 0; it < numIterations; ++it)
	{
		uint32 left = count;
		auto begin = pcCopy.begin();
		for (int i = 0; i < 8; ++i)
		{
			auto r = begin;
			uint32 offset = rng.randomUint32() % left;
			std::advance(r, offset);
			std::swap(*begin, *r);
			subset[i] = *begin;
			++begin;
			--left;
		}

		mat3 F = eightPointAlgorithm(subset);

		if (fuzzyEquals(F, mat3::identity))
		{
			continue;
		}

		int numInliers = 0;
		for (int i = 0; i < count; ++i)
		{
			vec3 cam(pc[i].camera.x, pc[i].camera.y, 1.f);
			vec3 proj(pc[i].projector.x, pc[i].projector.y, 1.f);

			vec3 camF = F * cam;
			float s2 = 1.f / dot(camF, camF);
			float d2 = dot(proj, camF);

			vec3 projF = transpose(F) * proj;
			float s1 = 1.f / dot(projF, projF);
			float d1 = dot(cam, projF);

			float err = max(d1 * d1 * s1, d2 * d2 * s2);

			if (err < tolerance)
			{
				tmpMask[i] = 1;
				++numInliers;
			}
			else
			{
				tmpMask[i] = 0;
			}
		}

		if (numInliers > bestNumInliers)
		{
			std::swap(outMask, tmpMask);
			bestNumInliers = numInliers;
			numIterations = ransacUpdateNumIters(confidence, (float)(count - numInliers) / count, 8, numIterations);
		}
	}

	if (bestNumInliers >= 8)
	{
		std::vector<pixel_correspondence> inliers;
		inliers.reserve(bestNumInliers);

		for (int i = 0; i < count; ++i)
		{
			if (outMask[i] == 1)
			{
				inliers.push_back(pc[i]);
			}
		}

		return eightPointAlgorithm(inliers);
	}
	return mat3::identity;
}
