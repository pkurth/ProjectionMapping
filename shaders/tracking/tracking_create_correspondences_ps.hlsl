#include "tracking_rs.hlsli"

ConstantBuffer<create_correspondences_cb> cb	: register(b0);
Texture2D<uint> cameraDepthTexture				: register(t0);
Texture2D<float2> cameraUnprojectTable			: register(t1);

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

	return float4(cameraNormal, 1.f);
}
