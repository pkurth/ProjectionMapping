#include "tracking_rs.hlsli"

struct ps_input
{
	float3 color	: COLOR;
};

[RootSignature(VISUALIZE_DEPTH_RS)]
float4 main(ps_input IN) : SV_TARGET0
{
	return float4(IN.color, 1.f);
}
