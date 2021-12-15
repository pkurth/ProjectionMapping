#include "cs.hlsli"
#include "projector_rs.hlsli"
#include "normal.hlsli"
#include "camera.hlsli"


ConstantBuffer<projector_confidence_cb> cb	: register(b0, space0);
StructuredBuffer<projector_cb> projectors	: register(t0, space0);

Texture2D<float4> renderResults[32]			: register(t0, space1);
Texture2D<float2> worldNormals[32]			: register(t0, space2);
Texture2D<float> depthTextures[32]			: register(t0, space3);
Texture2D<float> intensities[32]			: register(t0, space4);
Texture2D<float> masks[32]					: register(t0, space5);

RWTexture2D<float> outConfidences[32]		: register(u0, space0);


[numthreads(PROJECTOR_BLOCK_SIZE, PROJECTOR_BLOCK_SIZE, 1)]
[RootSignature(PROJECTOR_CONFIDENCE_RS)]
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
		outConfidences[index][texCoord] = 0.f;
		return;
	}

	const float2 uv = (float2(texCoord) + float2(0.5f, 0.5f)) * projectors[index].invScreenDims;

	const float3 color = renderResults[index][texCoord].rgb;
	const float3 P = restoreWorldSpacePosition(projectors[index].invViewProj, uv, depth);
	const float3 N = normalize(unpackNormal(worldNormals[index][texCoord]));
	const float mask = masks[index][texCoord];
	float3 V = projectors[index].position.xyz - P;
	const float distance = length(V);
	V *= rcp(distance);

	// Light intensity falls off quadratically with distance and with the cosine of the surface angle. Both effects need to be compensated.
	// In order to compensate colors which are too dark, we need to increase the brightness. This is only possible with some headroom in the projector's dynamic range.
	// We therefore define a desired white value (< 1, usually ~0.7) and a reference distance. These can be interpreted as follows:
	// If a projector displays the desired white value, a certain amount of light makes it over the reference distance.
	// This value acts as our reference. If a target point is farther away, we have to increase the brightness and vice-versa.
	// 'possibleWhiteIntensity' then represents, how much of 'white' this projector would be able to achieve.
	// saturate(1 / possibleWhiteIntensity) is the compensation, we would need to make to the projection.
	// Goal: Sum of possibleWhiteIntensity over all projectors must be 1.
	float possibleWhiteIntensity = getAngleAttenuation(N, V) * getDistanceAttenuation(distance, cb.referenceDistance) / cb.desiredWhiteValue;

	


	// Scale to match actual projection target color.
	float targetLuminance = dot(color, float3(0.21f, 0.71f, 0.08f));
	float possibleTargetIntensity = (targetLuminance < 0.001f) ? 1.f : (possibleWhiteIntensity / targetLuminance);




	float compensation = saturate(1.f / possibleTargetIntensity);



	//outConfidences[index][texCoord] = possiblePhysicalIntensity / denom;
}
