#include "calibration_rs.hlsli"

struct ps_input
{
	float4 color	: COLOR;
};

[RootSignature(VISUALIZE_POINT_CLOUD_RS)]
float4 main(ps_input IN) : SV_TARGET0
{
	return IN.color;
}
