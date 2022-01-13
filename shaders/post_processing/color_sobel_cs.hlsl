#include "cs.hlsli"
#include "post_processing_rs.hlsli"


ConstantBuffer<color_sobel_cb> cb	: register(b0);
RWTexture2D<float> output			: register(u0);
Texture2D<float4> input				: register(t0);

[numthreads(POST_PROCESSING_BLOCK_SIZE, POST_PROCESSING_BLOCK_SIZE, 1)]
[RootSignature(COLOR_SOBEL_RS)]
void main(cs_input IN)
{
	int2 xy = IN.dispatchThreadID.xy;

	const float3 lum = float3(0.21f, 0.71f, 0.08f);

	float tl = dot(lum, input[xy + int2(-1, -1)].rgb);
	float t  = dot(lum, input[xy + int2(0, -1)].rgb);
	float tr = dot(lum, input[xy + int2(1, -1)].rgb);
	float l  = dot(lum, input[xy + int2(-1, 0)].rgb);
	float r  = dot(lum, input[xy + int2(1, 0)].rgb);
	float bl = dot(lum, input[xy + int2(-1, 1)].rgb);
	float b  = dot(lum, input[xy + int2(0, 1)].rgb);
	float br = dot(lum, input[xy + int2(1, 1)].rgb);

	float horizontal = abs((tl + 2.f * t + tr) - (bl + 2.f * b + br));
	float vertical   = abs((tl + 2.f * l + bl) - (tr + 2.f * r + br));

	float edge = (horizontal > cb.threshold || vertical > cb.threshold) ? 1.f : 0.f;
	output[xy] = edge;
}

