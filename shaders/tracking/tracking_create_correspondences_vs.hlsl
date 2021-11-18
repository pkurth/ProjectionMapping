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

	float4 distorted = distort(OUT.position, cb.distortion);
	OUT.rtPosition = mul(cb.p, distorted);

	OUT.normal = mul(cb.m, float4(IN.normal, 0.f)).xyz;
	OUT.uv = IN.uv;

	return OUT;
}
