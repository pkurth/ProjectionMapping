#include "cs.hlsli"
#include "projector_rs.hlsli"
#include "camera.hlsli"
#include "normal.hlsli"

ConstantBuffer<projector_solver_cb> cb		: register(b0, space0);
StructuredBuffer<projector_cb> projectors	: register(t0, space0);

Texture2D<float4> renderResults[]			: register(t0, space1);
Texture2D<float2> worldNormals[]			: register(t0, space2);
Texture2D<float> depthTextures[]			: register(t0, space3);
Texture2D<float> intensities[]				: register(t0, space4);
Texture2D<float> masks[]					: register(t0, space5);

RWTexture2D<float> outIntensities[]			: register(u0, space0);

SamplerState borderSampler					: register(s0);



[RootSignature(PROJECTOR_SOLVER_RS)]
[numthreads(PROJECTOR_BLOCK_SIZE, PROJECTOR_BLOCK_SIZE, 1)]
void main(cs_input IN)
{
	uint index = cb.currentIndex;

	uint2 texCoord = IN.dispatchThreadID.xy;
	float2 dimensions = projectors[index].screenDims;
	if (texCoord.x >= (uint)dimensions.x || texCoord.y >= (uint)dimensions.y)
	{
		return;
	}

	float depth = depthTextures[index][texCoord];
	if (depth == 1.f)
	{
		outIntensities[index][texCoord] = 0.f;
		return;
	}

	uint numProjectors = cb.numProjectors;
	float2 invDimensions = projectors[index].invScreenDims;


	float2 uv = (float2(texCoord) + float2(0.5f, 0.5f)) * invDimensions;

	float3 worldPosition = restoreWorldSpacePosition(projectors[index].invViewProj, uv, depth);
	float3 N = normalize(unpackNormal(worldNormals[index][texCoord]));

	float maxPhysicalIntensityByOtherProjectors = 0.f;

	float intensityByOtherProjectors = 0.f;
	for (uint projIndex = 0; projIndex < numProjectors; ++projIndex)
	{
		if (projIndex != index)
		{
			float4 projected = mul(projectors[projIndex].viewProj, float4(worldPosition, 1.f));
			projected.xyz /= projected.w;

			float2 otherUV = projected.xy * 0.5f + float2(0.5f, 0.5f);
			otherUV.y = 1.f - otherUV.y;
			float testDepth = projected.z;

			float otherDepth = depthTextures[projIndex].SampleLevel(borderSampler, otherUV, 0);
			if (testDepth <= otherDepth + 0.00001f)
			{
				float distance = length(projectors[projIndex].position.xyz - worldPosition);
				float3 V = normalize(projectors[projIndex].position.xyz - worldPosition);
				
				float physicalIntensity = getAngleAttenuation(N, V) * getDistanceAttenuation(distance, cb.referenceDistance);
				float softwareIntensity = intensities[projIndex].SampleLevel(borderSampler, otherUV, 0);

				intensityByOtherProjectors += physicalIntensity * softwareIntensity;
				maxPhysicalIntensityByOtherProjectors = max(maxPhysicalIntensityByOtherProjectors, physicalIntensity);
			}
		}
	}

	const float desiredTotalIntensity = 0.7f;
	float remainingIntensity = desiredTotalIntensity - intensityByOtherProjectors;

	float distance = length(projectors[index].position.xyz - worldPosition);
	float3 V = normalize(projectors[index].position.xyz - worldPosition);
	float possiblePhysicalIntensity = getAngleAttenuation(N, V) * getDistanceAttenuation(distance, cb.referenceDistance);


	if (possiblePhysicalIntensity > maxPhysicalIntensityByOtherProjectors)
	{
		// We are the best hitting projector.
		remainingIntensity *= 1.001f;
	}


	float intensity = saturate(remainingIntensity / possiblePhysicalIntensity);
	//float mask = 1.f - masks[index][texCoord];
	//intensity *= mask;

	outIntensities[index][texCoord] = intensity;
}
