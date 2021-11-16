#pragma once

#include "math.h"
#include "physics/bounding_volumes.h"

union camera_frustum_corners
{
	vec3 eye;

	struct
	{
		vec3 nearTopLeft;
		vec3 nearTopRight;
		vec3 nearBottomLeft;
		vec3 nearBottomRight;
		vec3 farTopLeft;
		vec3 farTopRight;
		vec3 farBottomLeft;
		vec3 farBottomRight;
	};
	struct
	{
		vec3 corners[8];
	};

	camera_frustum_corners() {}
};

union camera_frustum_planes
{
	struct
	{
		vec4 nearPlane;
		vec4 farPlane;
		vec4 leftPlane;
		vec4 rightPlane;
		vec4 topPlane;
		vec4 bottomPlane;
	};
	vec4 planes[6];

	camera_frustum_planes() {}

	// Returns true, if object should be culled.
	bool cullWorldSpaceAABB(const bounding_box& aabb) const;
	bool cullModelSpaceAABB(const bounding_box& aabb, const trs& transform) const;
	bool cullModelSpaceAABB(const bounding_box& aabb, const mat4& transform) const;
};

enum camera_type
{
	camera_type_ingame,
	camera_type_calibrated,
};

struct camera_projection_extents
{
	float left, right, top, bottom; // Extents of frustum at distance 1.
};

struct camera_intrinsics
{
	float fx;
	float fy;
	float cx;
	float cy;
};

struct camera_distortion
{
	float k1;
	float k2;
	float k3;
	float k4;
	float k5;
	float k6;
	float p1;
	float p2;
};

struct render_camera
{
	// Camera properties.
	quat rotation;
	vec3 position;

	float nearPlane;
	float farPlane = -1.f;

	uint32 width, height;

	camera_type type;

	union
	{
		float verticalFOV; // For ingame cameras.
		camera_intrinsics intrinsics; // For calibrated cameras.
	};


	// Derived values.
	mat4 view;
	mat4 invView;

	mat4 proj;
	mat4 invProj;

	mat4 viewProj;
	mat4 invViewProj;
	
	float aspect;




	void initializeIngame(vec3 position, quat rotation, float verticalFOV, float nearPlane, float farPlane = -1.f);
	void initializeCalibrated(vec3 position, quat rotation, uint32 width, uint32 height, camera_intrinsics intr, float nearPlane, float farPlane = -1.f);

	void setViewport(uint32 width, uint32 height);

	void updateMatrices();





	ray generateWorldSpaceRay(float relX, float relY) const;
	ray generateViewSpaceRay(float relX, float relY) const;

	vec3 restoreViewSpacePosition(vec2 uv, float depthBufferDepth) const;
	vec3 restoreWorldSpacePosition(vec2 uv, float depthBufferDepth) const;
	float depthBufferDepthToEyeDepth(float depthBufferDepth) const;
	float eyeDepthToDepthBufferDepth(float eyeDepth) const;
	float linearizeDepthBuffer(float depthBufferDepth) const;

	camera_frustum_corners getWorldSpaceFrustumCorners(float alternativeFarPlane = -1.f) const;
	camera_frustum_planes getWorldSpaceFrustumPlanes() const;

	camera_frustum_corners getViewSpaceFrustumCorners(float alternativeFarPlane = -1.f) const;

	camera_projection_extents getProjectionExtents() const;
	float getMinProjectionExtent() const;


	vec4 getShaderProjectionParams() const;

	render_camera getJitteredVersion(vec2 offset) const;
};


camera_frustum_planes getWorldSpaceFrustumPlanes(const mat4& viewProj);
