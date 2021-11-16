#include "tracking_rs.hlsli"


struct ps_input
{
	float2 uv				: TEXCOORDS;
	float3 position			: POSITION;
	float3 normal			: NORMAL;
};

[RootSignature(CREATE_CORRESPONDENCES_RS)]
float4 main(ps_input IN) : SV_TARGET0
{
	return float4(IN.normal / 2.f, 1.f);
}
