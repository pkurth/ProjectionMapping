#include "pch.h"
#include "math_double.h"

const mat3d mat3d::identity =
{
	1., 0., 0.,
	0., 1., 0.,
	0., 0., 1.,
};

const mat3d mat3d::zero =
{
	0., 0., 0.,
	0., 0., 0.,
	0., 0., 0.,
};

const quatd quatd::identity = { 0., 0., 0., 1. };

mat3d operator*(const mat3d& a, const mat3d& b)
{
	vec3d r0 = row(a, 0);
	vec3d r1 = row(a, 1);
	vec3d r2 = row(a, 2);

	vec3d c0 = col(b, 0);
	vec3d c1 = col(b, 1);
	vec3d c2 = col(b, 2);

	mat3d result;
	result.m00 = dot(r0, c0); result.m01 = dot(r0, c1); result.m02 = dot(r0, c2);
	result.m10 = dot(r1, c0); result.m11 = dot(r1, c1); result.m12 = dot(r1, c2);
	result.m20 = dot(r2, c0); result.m21 = dot(r2, c1); result.m22 = dot(r2, c2);
	return result;
}

mat3d transpose(const mat3d& a)
{
	mat3d result;
	result.m00 = a.m00; result.m01 = a.m10; result.m02 = a.m20;
	result.m10 = a.m01; result.m11 = a.m11; result.m12 = a.m21;
	result.m20 = a.m02; result.m21 = a.m12; result.m22 = a.m22;
	return result;
}

mat3d quaternionToMat3(quatd q)
{
	if (q.w == 1.)
	{
		return mat3d::identity;
	}

	double qxx = q.x * q.x;
	double qyy = q.y * q.y;
	double qzz = q.z * q.z;
	double qxz = q.x * q.z;
	double qxy = q.x * q.y;
	double qyz = q.y * q.z;
	double qwx = q.w * q.x;
	double qwy = q.w * q.y;
	double qwz = q.w * q.z;

	mat3d result;

	result.m00 = 1. - 2. * (qyy + qzz);
	result.m10 = 2. * (qxy + qwz);
	result.m20 = 2. * (qxz - qwy);

	result.m01 = 2. * (qxy - qwz);
	result.m11 = 1. - 2. * (qxx + qzz);
	result.m21 = 2. * (qyz + qwx);

	result.m02 = 2. * (qxz + qwy);
	result.m12 = 2. * (qyz - qwx);
	result.m22 = 1. - 2. * (qxx + qyy);

	return result;
}

quatd mat3ToQuaternion(const mat3d& m)
{
#if 1
	double tr = m.m00 + m.m11 + m.m22;

	quatd result;
	if (tr > 0.)
	{
		double s = sqrt(tr + 1.) * 2.; // S=4*qw 
		result.w = 0.25 * s;
		result.x = (m.m21 - m.m12) / s;
		result.y = (m.m02 - m.m20) / s;
		result.z = (m.m10 - m.m01) / s;
	}
	else if ((m.m00 > m.m11) && (m.m00 > m.m22))
	{
		double s = sqrt(1. + m.m00 - m.m11 - m.m22) * 2.; // S=4*qx 
		result.w = (m.m21 - m.m12) / s;
		result.x = 0.25 * s;
		result.y = (m.m01 + m.m10) / s;
		result.z = (m.m02 + m.m20) / s;
	}
	else if (m.m11 > m.m22)
	{
		double s = sqrt(1. + m.m11 - m.m00 - m.m22) * 2.; // S=4*qy
		result.w = (m.m02 - m.m20) / s;
		result.x = (m.m01 + m.m10) / s;
		result.y = 0.25 * s;
		result.z = (m.m12 + m.m21) / s;
	}
	else
	{
		double s = sqrt(1. + m.m22 - m.m00 - m.m11) * 2.; // S=4*qz
		result.w = (m.m10 - m.m01) / s;
		result.x = (m.m02 + m.m20) / s;
		result.y = (m.m12 + m.m21) / s;
		result.z = 0.25 * s;
	}
#else
	quat result;
	result.w = sqrt(1. + m.m00 + m.m11 + m.m22) * 0.5;
	double w4 = 1. / (4. * result.w);
	result.x = (m.m21 - m.m12) * w4;
	result.y = (m.m02 - m.m20) * w4;
	result.z = (m.m10 - m.m02) * w4;
#endif
	return normalize(result);
}

double determinant(const mat3d& m)
{
	return m.m00 * (m.m11 * m.m22 - m.m21 * m.m12)
		- m.m01 * (m.m10 * m.m22 - m.m20 * m.m12)
		+ m.m02 * (m.m10 * m.m21 - m.m20 * m.m11);
}

mat3d::mat3d(double m00, double m01, double m02, double m10, double m11, double m12, double m20, double m21, double m22)
	:
	m00(m00), m01(m01), m02(m02),
	m10(m10), m11(m11), m12(m12),
	m20(m20), m21(m21), m22(m22) {}
