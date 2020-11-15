#ifndef CAMERA_H
#define CAMERA_H

struct camera_cb
{
	mat4 viewProj;
	mat4 view;
	mat4 proj;
	mat4 invViewProj;
	mat4 invView;
	mat4 invProj;
	vec4 position;
	vec4 forward;
	vec4 projectionParams; // nearPlane, farPlane, farPlane / nearPlane, 1 - farPlane / nearPlane
	vec2 screenDims;
	vec2 invScreenDims;
};

#ifdef HLSL
static float3 restoreViewSpacePosition(float4x4 invProj, float2 uv, float depth)
{
	uv.y = 1.f - uv.y; // Screen uvs start at the top left, so flip y.
	float3 ndc = float3(uv * 2.f - float2(1.f, 1.f), depth);
	float4 homPosition = mul(invProj, float4(ndc, 1.f));
	float3 position = homPosition.xyz / homPosition.w;
	return position;
}

static float3 restoreWorldSpacePosition(float4x4 invViewProj, float2 uv, float depth)
{
	uv.y = 1.f - uv.y; // Screen uvs start at the top left, so flip y.
	float3 ndc = float3(uv * 2.f - float2(1.f, 1.f), depth);
	float4 homPosition = mul(invViewProj, float4(ndc, 1.f));
	float3 position = homPosition.xyz / homPosition.w;
	return position;
}

static float3 restoreViewDirection(float4x4 invProj, float2 uv)
{
	return restoreViewSpacePosition(invProj, uv, 1.f);
}

static float3 restoreWorldDirection(float4x4 invViewProj, float2 uv, float3 cameraPos, float farPlane)
{
	float3 direction = restoreWorldSpacePosition(invViewProj, uv, 1.f) - cameraPos; // At this point, the result should be on a plane 'farPlane' units away from the camera.
	direction /= farPlane;
	return direction;
}

// This function returns a positive z value! This is a depth!
static float depthBufferDepthToEyeDepth(float depthBufferDepth, float4 projectionParams)
{
	if (projectionParams.y < 0.f) // Infinite far plane.
	{
		depthBufferDepth = clamp(depthBufferDepth, 0.f, 1.f - 1e-7f); // A depth of 1 is at infinity.
		return -projectionParams.x / (depthBufferDepth - 1.f);
	}
	else
	{
		const float c1 = projectionParams.z;
		const float c0 = projectionParams.w;
		return projectionParams.y / (c0 * depthBufferDepth + c1);
	}
}

struct camera_frustum_planes
{
	float4 planes[6];
};

// Returns true, if object should be culled.
static bool cullWorldSpaceAABB(camera_frustum_planes planes, float4 min, float4 max)
{
	for (uint i = 0; i < 6; ++i)
	{
		float4 plane = planes.planes[i];
		float4 vertex = float4(
			(plane.x < 0.f) ? min.x : max.x,
			(plane.y < 0.f) ? min.y : max.y,
			(plane.z < 0.f) ? min.z : max.z,
			1.f
			);
		if (dot(plane, vertex) < 0.f)
		{
			return true;
		}
	}
	return false;
}

static bool cullModelSpaceAABB(camera_frustum_planes planes, float4 min, float4 max, float4x4 transform)
{
	float4 worldSpaceCorners[] =
	{
		mul(transform, float4(min.x, min.y, min.z, 1.f)),
		mul(transform, float4(max.x, min.y, min.z, 1.f)),
		mul(transform, float4(min.x, max.y, min.z, 1.f)),
		mul(transform, float4(max.x, max.y, min.z, 1.f)),
		mul(transform, float4(min.x, min.y, max.z, 1.f)),
		mul(transform, float4(max.x, min.y, max.z, 1.f)),
		mul(transform, float4(min.x, max.y, max.z, 1.f)),
		mul(transform, float4(max.x, max.y, max.z, 1.f)),
	};

	for (uint i = 0; i < 6; ++i)
	{
		float4 plane = planes.planes[i];

		bool inside = false;

		for (uint j = 0; j < 8; ++j)
		{
			if (dot(plane, worldSpaceCorners[j]) > 0.f)
			{
				inside = true;
				break;
			}
		}

		if (!inside)
		{
			return true;
		}
	}

	return false;
}
#endif

#endif
