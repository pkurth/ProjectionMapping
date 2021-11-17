#include "tracking_rs.hlsli"

ConstantBuffer<create_correspondences_vs_cb> cb : register(b0);

struct vs_input
{
	float3 position		: POSITION;
	float2 uv			: TEXCOORDS;
	float3 normal		: NORMAL;
	float3 tangent		: TANGENT;
};

struct vs_output
{
	float2 uv				: TEXCOORDS;
	float3 position			: POSITION;
	float3 normal			: NORMAL;

	float4 rtPosition		: SV_POSITION;
};

[RootSignature(CREATE_CORRESPONDENCES_DEPTH_ONLY_RS)]
vs_output main(vs_input IN)
{
	vs_output OUT;

	OUT.position = mul(cb.m, float4(IN.position, 1.f)).xyz;

	float2 pixel = project(OUT.position, cb.intrinsics, cb.distortion);
	float2 screenUV = pixel / float2(cb.width, cb.height);
	screenUV.y = 1.f - screenUV.y;

	float2 xy = screenUV * 2.f - 1.f;
	OUT.rtPosition.z = -OUT.position.z - 0.001f;
	OUT.rtPosition.w = -OUT.position.z;
	OUT.rtPosition.xy = xy * OUT.rtPosition.w; // Because the hardware will divide by this, we pre-multiply to counteract this.

	OUT.normal = mul(cb.m, float4(IN.normal, 0.f)).xyz;
	OUT.uv = IN.uv;

	return OUT;
}
