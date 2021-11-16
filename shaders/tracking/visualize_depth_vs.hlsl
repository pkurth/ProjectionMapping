#include "tracking_rs.hlsli"

ConstantBuffer<visualize_depth_cb> cb	: register(b0);
Texture2D<uint> depthTexture			: register(t0);
Texture2D<float2> xyTable				: register(t1);

struct vs_input
{
	uint vertexID	: SV_VertexID;
};

struct vs_output
{
	float2 uv		: TEXCOORDS;
	float4 position : SV_Position;
};

vs_output main(vs_input IN)
{
	uint x = IN.vertexID % cb.width;
	uint y = IN.vertexID / cb.width;

	float depth = depthTexture[uint2(x, y)] * cb.depthScale;
	float3 pos = float3(xyTable[uint2(x, y)], -1.f) * depth;

	float4 colorProj = mul(cb.colorCameraVP, float4(pos, 1.f));
	float2 uv = (colorProj.xy / colorProj.w) * 0.5f + float2(0.5f, 0.5f);
	uv.y = 1.f - uv.y;

	vs_output OUT;
	OUT.position = mul(cb.vp, float4(pos, 1.f));
	OUT.uv = uv;

	return OUT;
}
