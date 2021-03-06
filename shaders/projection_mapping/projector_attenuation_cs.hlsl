#include "cs.hlsli"
#include "projector_rs.hlsli"
#include "normal.hlsli"
#include "camera.hlsli"


ConstantBuffer<projector_attenuation_cb> cb	: register(b0, space0);
StructuredBuffer<projector_cb> projectors	: register(t0, space0);

Texture2D<float4> renderResults[32]			: register(t0, space1);
Texture2D<float2> worldNormals[32]			: register(t0, space2);
Texture2D<float> depthTextures[32]			: register(t0, space3);

RWTexture2D<float3> output[32]				: register(u0, space0);

SamplerState clampSampler					: register(s0);




[numthreads(PROJECTOR_BLOCK_SIZE, PROJECTOR_BLOCK_SIZE, 1)]
[RootSignature(PROJECTOR_ATTENUATION_RS)]
void main(cs_input IN)
{
	uint index = cb.index;

	uint2 texCoord = IN.dispatchThreadID.xy;
	float2 dimensions = projectors[index].screenDims;
	if (texCoord.x >= (uint)dimensions.x || texCoord.y >= (uint)dimensions.y)
	{
		return;
	}

	const float depth = depthTextures[index][texCoord];
	if (depth == 1.f)
	{
		output[index][texCoord] = (float3)0.f;
		return;
	}

	const float2 uv = (float2(texCoord) + float2(0.5f, 0.5f)) * projectors[index].invScreenDims;

	const float3 color = renderResults[index][texCoord].rgb;
	const float3 P = restoreWorldSpacePosition(projectors[index].invViewProj, uv, depth);
	const float3 N = normalize(unpackNormal(worldNormals[index][texCoord]));
	float3 V = projectors[index].position.xyz - P;
	const float distance = length(V);
	V *= rcp(distance);


	// Light intensity falls off with the surface angle (cosine) and distance (quadratic). Combined we call this the attenuation.
	// For distance-based falloff we define a reference distance. Points closer need to be dimmed down, points farther away need to be brightened.
	// In order to compensate colors which are too dark, we need to increase the brightness. This is only possible with some headroom in the projector's dynamic range.
	// We therefore define a desired white value (< 1, usually ~0.7).
	// The light transport is then: 
	// SUM (atten * comp * lum) = lum * desWhite,
	// where SUM is a sum over all projectors, atten is the attenuation, lum is the brightness we want to achieve and comp is the artificial brightness scale we apply 
	// in software to compensate for the attenuation. Therefore comp is the variable we solve for.
	// In the equation lum cancels out, leaving us with:
	// SUM (atten * comp) = desWhite.

	float attenuation = getAngleAttenuation(N, V) * getDistanceAttenuation(distance, cb.referenceDistance);
	float possibleWhiteIntensity = attenuation / cb.desiredWhiteValue;

	float maxComponent = max(color.r, max(color.g, color.b));

#if 0
	float d = depthBufferDepthToEyeDepth(depth, projectors[index].projectionParams);
	float offsetFromOptimal = abs(d - projectors[index].optimalDepth);
	float depthRangeWeight = 1.f - 0.8f * smoothstep(0.f, projectors[index].depthRange, offsetFromOptimal);
#else
	float depthRangeWeight = 1.f;
#endif


	const float k = 4.f;
	float E = pow(possibleWhiteIntensity, k) * depthRangeWeight;


	output[index][texCoord] = float3(possibleWhiteIntensity, E, maxComponent);
}
