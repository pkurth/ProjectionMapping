#include "cs.hlsli"
#include "post_processing_rs.hlsli"
#include "hbao_blur_common.hlsli"
#include "math.hlsli"

Texture2D<float2> motion            : register(t2);
Texture2D<float> history            : register(t3);

[RootSignature(HBAO_BLUR_Y_RS)]
[numthreads(POST_PROCESSING_BLOCK_SIZE, POST_PROCESSING_BLOCK_SIZE, 1)]
void main(cs_input IN)
{
	float ao = blur(float2(0.f, 1.f), IN.dispatchThreadID.xy);

#if 1
	float2 uv = (IN.dispatchThreadID.xy + float2(0.5f, 0.5f)) * cb.invDimensions;

	const float2 m = motion[IN.dispatchThreadID.xy].xy;
	const float2 prevUV = uv + m;

	float prevAO = history.SampleLevel(linearSampler, prevUV, 0);

	float subpixelCorrection = frac(
		max(
			abs(m.x) * cb.dimensions.x,
			abs(m.y) * cb.dimensions.y
		)
	) * 0.5f;

	float blendfactor = saturate(lerp(0.05f, 0.8f, subpixelCorrection));
	blendfactor = isSaturated(prevUV) ? blendfactor : 1.f;
	blendfactor = (abs(ao - prevAO) < 0.1f) ? blendfactor : 1.f;

	ao = lerp(prevAO, ao, blendfactor);
#endif

	output[IN.dispatchThreadID.xy] = ao;
}
