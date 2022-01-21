#include "cs.hlsli"
#include "projector_rs.hlsli"
#include "camera.hlsli"

ConstantBuffer<projector_intensity_cb> cb		: register(b0, space0);
StructuredBuffer<projector_cb> allProjectors	: register(t0, space0);

Texture2D<float4> confidenceTextures[32]		: register(t0, space1);
Texture2D<float> depthTextures[32]				: register(t0, space2);
Texture2D<float> bestMaskTextures[32]			: register(t0, space3);

RWTexture2D<float> outIntensities[32]			: register(u0, space0);

SamplerState borderSampler						: register(s0);
SamplerState depthSampler						: register(s1);


struct projector_data
{
	float attenuation;
	float E;
	float solverIntensity;
	float partialSum;
};

static projector_data fillOutData(float4 c)
{
	float attenuation = c.x;
	float depthMask = c.z;
	float colorMask = c.w;

	float k = 4.f;
	//float k = lerp(1.f, 8.f, colorMask);
	projector_data result = { attenuation, pow(attenuation, k) * depthMask, 0.f, 0.f };
	return result;
}

[numthreads(PROJECTOR_BLOCK_SIZE, PROJECTOR_BLOCK_SIZE, 1)]
[RootSignature(PROJECTOR_INTENSITIES_RS)]
void main(cs_input IN)
{
	uint index = cb.index;

	uint2 texCoord = IN.dispatchThreadID.xy;
	float2 dimensions = allProjectors[index].screenDims;
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

	float2 invDimensions = allProjectors[index].invScreenDims;
	float2 uv = (float2(texCoord) + float2(0.5f, 0.5f)) * invDimensions;

	float3 P = restoreWorldSpacePosition(allProjectors[index].invViewProj, uv, depth);

	projector_data projectors[16];

	float ESum = 0.f;

	projectors[0] = fillOutData(confidenceTextures[index][texCoord]);
	ESum += projectors[0].E;

	uint myProjectorIndex = 0;
	uint numProjectors = 1;


	float targetIntensity = max(confidenceTextures[index][texCoord].y, 0.001f);


	for (uint projIndex = 0; projIndex < cb.numProjectors; ++projIndex)
	{
		if (projIndex != index)
		{
			float4 projected = mul(allProjectors[projIndex].viewProj, float4(P, 1.f));
			projected.xyz /= projected.w;

			float2 projUV = projected.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
			float testDepth = projected.z;

			float projDepth = depthTextures[projIndex].SampleLevel(depthSampler, projUV, 0);
			if (projDepth < 1.f && testDepth <= projDepth + 0.00005f)
			{
				projectors[numProjectors] = fillOutData(confidenceTextures[projIndex].SampleLevel(borderSampler, projUV, 0));
				ESum += projectors[numProjectors].E;
				++numProjectors;
			}
		}
	}


	float remainingIntensity = targetIntensity;

	const uint numIterations = 3;

	uint iteration = 0;
	for (; iteration < numIterations && numProjectors > 0; ++iteration)
	{
		float nextESum = ESum;
		float resultingIntensity = 0.f;


		for (uint projIndex = 0; projIndex < numProjectors; ++projIndex)
		{
			float maxCompensation = (1.f - projectors[projIndex].partialSum) / remainingIntensity;

			float w = projectors[projIndex].E / ESum;
			float g = clamp(w / projectors[projIndex].attenuation, 0.f, maxCompensation);

			resultingIntensity += projectors[projIndex].attenuation * g * remainingIntensity;

			projectors[projIndex].partialSum += g * remainingIntensity;

			if (g == maxCompensation)
			{
				// Projector intensity is depleted. Remove from list for next iteration.
				//assert(fuzzyEquals(proj.partialSum, 1.f));

				nextESum -= projectors[projIndex].E;



				if (myProjectorIndex == projIndex)
				{
					myProjectorIndex = numProjectors - 1;
				}
				else if (myProjectorIndex == numProjectors - 1)
				{
					myProjectorIndex = projIndex;
				}

				// Swap (don't overwrite), so that we can recover our projector of interest.
				projector_data temp = projectors[projIndex];
				projectors[projIndex] = projectors[numProjectors - 1];
				projectors[numProjectors - 1] = temp;

				--numProjectors;
				--projIndex;
			}
		}





		if (resultingIntensity >= remainingIntensity - 0.001f)
		{
			++iteration; // Just for debug output.
			break;
		}

		remainingIntensity -= resultingIntensity;
		ESum = nextESum;
	}



	projector_data myProj = projectors[myProjectorIndex];
	float solverIntensity = myProj.partialSum / targetIntensity;
	outIntensities[index][texCoord] = max(0.f, solverIntensity);

	outIntensities[index][texCoord] = solverIntensity;
}
