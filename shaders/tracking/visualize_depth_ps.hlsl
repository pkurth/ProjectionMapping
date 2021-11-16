#include "tracking_rs.hlsli"


Texture2D<float4> colorTexture	: register(t0, space1);
SamplerState colorTextureSampler : register(s0);

struct ps_input
{
	float2 uv		: TEXCOORDS;
};

[RootSignature(VISUALIZE_DEPTH_RS)]
float4 main(ps_input IN) : SV_TARGET0
{
	return colorTexture.Sample(colorTextureSampler, IN.uv);
}
