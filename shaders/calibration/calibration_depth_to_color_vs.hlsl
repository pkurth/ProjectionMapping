#include "calibration_rs.hlsli"

ConstantBuffer<depth_to_color_cb> cb : register(b0);

struct vs_input
{
	float3 position		: POSITION;
	float2 uv			: TEXCOORDS;
	float3 normal		: NORMAL;
	float3 tangent		: TANGENT;
};

struct vs_output
{
	float viewDepth			: DEPTH;
	float3 viewNormal		: NORMAL;

	float4 rtPosition		: SV_POSITION;
};

vs_output main(vs_input IN)
{
	vs_output OUT;

	float3 viewPosition = mul(cb.mv, float4(IN.position, 1.f)).xyz;

	float4 distorted = distort(viewPosition, cb.distortion);
	OUT.rtPosition = mul(cb.p, distorted);

	OUT.viewDepth = -viewPosition.z;
	OUT.viewNormal = mul(cb.mv, float4(IN.normal, 0.f)).xyz;

	return OUT;
}
