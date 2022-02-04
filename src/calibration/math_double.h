#pragma once

struct vec3d
{
	double x, y, z;
};

union mat3d
{
#if ROW_MAJOR
	struct
	{
		double
			m00, m01, m02,
			m10, m11, m12,
			m20, m21, m22;
	}; 
	struct
	{
		vec3d row0;
		vec3d row1;
		vec3d row2;
	};
	vec3d rows[3];
#else
	struct
	{
		double
			m00, m10, m20,
			m01, m11, m21,
			m02, m12, m22;
	};
	struct
	{
		vec3d col0;
		vec3d col1;
		vec3d col2;
	};
	vec3d cols[3];
#endif
	double m[9];

	mat3d() {}
	mat3d(
		double m00, double m01, double m02,
		double m10, double m11, double m12,
		double m20, double m21, double m22);

	static const mat3d identity;
	static const mat3d zero;
};

union quatd
{
	struct
	{
		double x, y, z, w;
	};
	struct
	{
		vec3d v;
		double cosHalfAngle;
	};
	double data[4];

	quatd() {}
	quatd(double x, double y, double z, double w) : x(x), y(y), z(z), w(w) {}

	static const quatd identity;
};

#if ROW_MAJOR
static vec3d row(const mat3d& a, uint32 r) { return a.rows[r]; }
static vec3d col(const mat3d& a, uint32 c) { return { a.m[c], a.m[c + 3], a.m[c + 6] }; }
#else
static vec3d row(const mat3d& a, uint32 r) { return { a.m[r], a.m[r + 3], a.m[r + 6] }; }
static vec3d col(const mat3d& a, uint32 c) { return a.cols[c]; }
#endif


static vec3d operator+(vec3d a, vec3d b) { vec3d result = { a.x + b.x, a.y + b.y, a.z + b.z }; return result; }
static vec3d& operator+=(vec3d& a, vec3d b) { a = a + b; return a; }
static vec3d operator-(vec3d a, vec3d b) { vec3d result = { a.x - b.x, a.y - b.y, a.z - b.z }; return result; }
static vec3d& operator-=(vec3d& a, vec3d b) { a = a - b; return a; }
static vec3d operator*(vec3d a, vec3d b) { vec3d result = { a.x * b.x, a.y * b.y, a.z * b.z }; return result; }
static vec3d& operator*=(vec3d& a, vec3d b) { a = a * b; return a; }
static vec3d operator/(vec3d a, vec3d b) { vec3d result = { a.x / b.x, a.y / b.y, a.z / b.z }; return result; }
static vec3d& operator/=(vec3d& a, vec3d b) { a = a / b; return a; }

static vec3d operator*(vec3d a, double b) { vec3d result = { a.x * b, a.y * b, a.z * b }; return result; }
static vec3d operator*(double a, vec3d b) { return b * a; }
static vec3d& operator*=(vec3d& a, double b) { a = a * b; return a; }
static vec3d operator/(vec3d a, double b) { vec3d result = { a.x / b, a.y / b, a.z / b }; return result; }
static vec3d& operator/=(vec3d& a, double b) { a = a / b; return a; }

static vec3d operator-(vec3d a) { return vec3d{ -a.x, -a.y, -a.z }; }

static bool operator==(vec3d a, vec3d b) { return a.x == b.x && a.y == b.y && a.z == b.z; }


static double dot(vec3d a, vec3d b) { double result = a.x * b.x + a.y * b.y + a.z * b.z; return result; }
static vec3d operator*(mat3d a, vec3d b) { vec3d result = { dot(row(a, 0), b), dot(row(a, 1), b), dot(row(a, 2), b) }; return result; }
mat3d operator*(const mat3d& a, const mat3d& b);

static mat3d operator*(const mat3d& a, double b) { mat3d result; for (uint32 i = 0; i < 9; ++i) { result.m[i] = a.m[i] * b; } return result; }

mat3d transpose(const mat3d& a);
double determinant(const mat3d& m);

static quatd normalize(quatd q) { double l = 1. / sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w); return { q.x * l, q.y * l, q.z * l, q.w * l }; }

static bool fuzzyEquals(double a, double b, double threshold = 1e-5) { return abs(a - b) < threshold; }

mat3d quaternionToMat3(quatd q);
quatd mat3ToQuaternion(const mat3d& m);

static bool fuzzyEquals(const mat3d& a, const mat3d& b, double threshold = 1e-4)
{
	bool result = true;
	for (uint32 i = 0; i < 9; ++i)
	{
		result &= fuzzyEquals(a.m[i], b.m[i], threshold);
	}
	return result;
}
