#include "calibration_rs.hlsli"

struct ps_input
{
	float viewDepth			: DEPTH;
	float3 viewNormal		: NORMAL;
};

[RootSignature(DEPTH_TO_COLOR_RS)]
float4 main(ps_input IN) : SV_TARGET0
{
	return float4(normalize(IN.viewNormal), IN.viewDepth);
}