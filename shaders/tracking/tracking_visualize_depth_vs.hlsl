#include "tracking_rs.hlsli"

ConstantBuffer<visualize_depth_cb> cb	: register(b0);
Texture2D<uint> depthTexture			: register(t0);
Texture2D<float2> unprojectTable		: register(t1);

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
	uint x = IN.vertexID % cb.depthWidth;
	uint y = IN.vertexID / cb.depthWidth;

	float depth = depthTexture[uint2(x, y)] * cb.depthScale;
	float3 pos = float3(unprojectTable[uint2(x, y)], -1.f) * depth;


	float3 colorPos = mul(cb.colorCameraV, float4(pos, 1.f)).xyz;
    float2 colorPixel = project(colorPos, cb.colorCameraIntrinsics, cb.colorCameraDistortion);
    float2 uv = colorPixel / float2(cb.colorWidth, cb.colorHeight);


	vs_output OUT;
	OUT.position = mul(cb.vp, float4(pos, 1.f));
	OUT.uv = uv;

	return OUT;
}
