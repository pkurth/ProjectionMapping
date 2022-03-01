#include "cs.hlsli"
#include "post_processing_rs.hlsli"
#include "camera.hlsli"


ConstantBuffer<combined_sobel_cb> cb	: register(b0);

RWTexture2D<float2> output				: register(u0);

Texture2D<float> depth					: register(t0);
Texture2D<float4> color					: register(t1);

static float getDepthAt(int2 c, float centerD)
{
	float d = centerD;
	if (all(c >= 0) && all(c < int2(cb.resolutionX, cb.resolutionY)))
	{
		d = depth[c];
	}
	return depthBufferDepthToEyeDepth(d, cb.projectionParams);
}

[numthreads(POST_PROCESSING_BLOCK_SIZE, POST_PROCESSING_BLOCK_SIZE, 1)]
[RootSignature(COMBINED_SOBEL_RS)]
void main(cs_input IN)
{
	int2 xy = IN.dispatchThreadID.xy;

	float depthEdge = 0.f;
	float colorEdge = 0.f;


	{
		float center = depth[xy];

		float tl = getDepthAt(xy + int2(-1, -1), center);
		float t = getDepthAt(xy + int2(0, -1), center);
		float tr = getDepthAt(xy + int2(1, -1), center);
		float l = getDepthAt(xy + int2(-1, 0), center);
		float r = getDepthAt(xy + int2(1, 0), center);
		float bl = getDepthAt(xy + int2(-1, 1), center);
		float b = getDepthAt(xy + int2(0, 1), center);
		float br = getDepthAt(xy + int2(1, 1), center);

		float horizontal = abs((tl + 2.f * t + tr) - (bl + 2.f * b + br));
		float vertical = abs((tl + 2.f * l + bl) - (tr + 2.f * r + br));

		depthEdge = (horizontal > cb.depthThreshold || vertical > cb.depthThreshold) ? 1.f : 0.f;
	}

	{
		const float3 lum = float3(0.21f, 0.71f, 0.08f);

		float3 tl = color[xy + int2(-1, -1)].rgb;
		float3 t = color[xy + int2(0, -1)].rgb;
		float3 tr = color[xy + int2(1, -1)].rgb;
		float3 l = color[xy + int2(-1, 0)].rgb;
		float3 r = color[xy + int2(1, 0)].rgb;
		float3 bl = color[xy + int2(-1, 1)].rgb;
		float3 b = color[xy + int2(0, 1)].rgb;
		float3 br = color[xy + int2(1, 1)].rgb;

		float3 horizontal = abs((tl + 2.f * t + tr) - (bl + 2.f * b + br));
		float3 vertical = abs((tl + 2.f * l + bl) - (tr + 2.f * r + br));

		colorEdge = any(horizontal > cb.colorThreshold || vertical > cb.colorThreshold) ? 1.f : 0.f;
	}

	output[xy] = float2(depthEdge, colorEdge);
}

