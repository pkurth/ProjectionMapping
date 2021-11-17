#include "tracking_rs.hlsli"

ConstantBuffer<create_correspondences_ps_cb> cb		: register(b1);
Texture2D<uint> cameraDepthTexture					: register(t0);
Texture2D<float2> cameraUnprojectTable				: register(t1);

RWStructuredBuffer<tracking_correspondence> output	: register(u0);
RWStructuredBuffer<uint> counter					: register(u1);

struct ps_input
{
	float2 uv				: TEXCOORDS;
	float3 position			: POSITION;
	float3 normal			: NORMAL;

	float4 screenPosition	: SV_POSITION;
};

#define NORMAL_RECONSTRUCTION_ACCURACY 2

[RootSignature(CREATE_CORRESPONDENCES_RS)]
float4 main(ps_input IN) : SV_TARGET0
{
	uint2 xy = (uint2)IN.screenPosition.xy;

	float3 cameraPosition = float3(cameraUnprojectTable[xy], -1.f) * (cameraDepthTexture[xy] * cb.depthScale);
	float3 offset = cameraPosition - IN.position;

	if (dot(offset, offset) > cb.squaredPositionThreshold)
	{
		return float4(0.f, 0.f, 0.f, 0.f);
	}


#if NORMAL_RECONSTRUCTION_ACCURACY == 0
	float3 cameraNormal = normalize(cross(ddy(cameraPosition), ddx(cameraPosition)));

#elif NORMAL_RECONSTRUCTION_ACCURACY == 1
	float3 cameraPositionX = float3(cameraUnprojectTable[xy + uint2(1, 0)], -1.f) * (cameraDepthTexture[xy + uint2(1, 0)] * cb.depthScale);
	float3 cameraPositionY = float3(cameraUnprojectTable[xy + uint2(0, 1)], -1.f) * (cameraDepthTexture[xy + uint2(0, 1)] * cb.depthScale);
	float3 cameraNormal = normalize(cross(cameraPositionY - cameraPosition, cameraPositionX - cameraPosition));

#elif NORMAL_RECONSTRUCTION_ACCURACY == 2
	float3 cameraPositionR = float3(cameraUnprojectTable[xy + uint2(1, 0)], -1.f) * (cameraDepthTexture[xy + uint2(1, 0)] * cb.depthScale);
	float3 cameraPositionL = float3(cameraUnprojectTable[xy - uint2(1, 0)], -1.f) * (cameraDepthTexture[xy - uint2(1, 0)] * cb.depthScale);
	float3 cameraPositionB = float3(cameraUnprojectTable[xy + uint2(0, 1)], -1.f) * (cameraDepthTexture[xy + uint2(0, 1)] * cb.depthScale);
	float3 cameraPositionT = float3(cameraUnprojectTable[xy - uint2(0, 1)], -1.f) * (cameraDepthTexture[xy - uint2(0, 1)] * cb.depthScale);

	float3 R = cameraPositionR - cameraPosition;
	float3 L = cameraPosition - cameraPositionL;
	float3 B = cameraPositionB - cameraPosition;
	float3 T = cameraPosition - cameraPositionT;

	float3 hor = (abs(L.z) < abs(R.z)) ? L : R;
	float3 ver = (abs(B.z) < abs(T.z)) ? B : T;

	float3 cameraNormal = normalize(cross(ver, hor));
#endif

	float3 normal = normalize(IN.normal);
	float cosAngle = dot(normal, cameraNormal);

	if (cosAngle < cb.cosAngleThreshold)
	{
		return float4(0.f, 0.f, 0.f, 0.f);
	}


#if 0
	tracking_correspondence result;

	if (cb.trackingDirection == tracking_direction_camera_to_render)
	{
		result.grad0 = float4(
			cross(cameraPosition, normal),
			dot(-offset, normal)
		);
		result.grad1 = float4(normal, 1.f);
	}
	else // if (cb.trackingDirection == tracking_direction_render_to_camera)
	{
		result.grad0 = float4(
			cross(IN.position, cameraNormal),
			dot(offset, cameraNormal)
		);
		result.grad1 = float4(cameraNormal, 1.f);
	}

	uint index;
	InterlockedAdd(counter[0], 1, index);

	output[index] = result;
#endif

	return float4(cameraNormal, 1.f);
}
