#include "cs.hlsli"
#include "projector_rs.hlsli"
#include "camera.hlsli"

ConstantBuffer<projector_solver_cb> cb  : register(b0, space0);
StructuredBuffer<projector_vp> vps      : register(t0, space0);

Texture2D<float4> renderResults[]       : register(t0, space1);
Texture2D<float> depthTextures[]        : register(t0, space2);
Texture2D<float> intensities[]          : register(t0, space3);

RWTexture2D<float> outCurrentIntensity  : register(u0, space0);

SamplerState borderSampler				: register(s0);


[RootSignature(PROJECTOR_SOLVER_RS)]
[numthreads(PROJECTOR_SOLVER_BLOCK_SIZE, PROJECTOR_SOLVER_BLOCK_SIZE, 1)]
void main(cs_input IN)
{
	uint2 texCoord = IN.dispatchThreadID.xy;
	if (texCoord.x >= cb.width || texCoord.y >= cb.height)
	{
		return;
	}

	float2 uv = (float2(texCoord) + float2(0.5f, 0.5f)) / float2(cb.width, cb.height);

	uint index = cb.currentIndex;
	uint numProjectors = cb.numProjectors;
	float depth = depthTextures[index][texCoord];

	float4x4 invVP = vps[index].invViewProj;
	float3 worldPosition = restoreWorldSpacePosition(invVP, uv, depth);

	float intensityByOtherProjectors = 0.f;
	for (uint proj = 0; proj < numProjectors; ++proj)
	{
		if (proj != index)
		{
			float4 projected = mul(vps[proj].viewProj, float4(worldPosition, 1.f));
			projected.xyz /= projected.w;

			float2 otherUV = projected.xy * 0.5f + float2(0.5f, 0.5f);
			otherUV.y = 1.f - otherUV.y;
			float testDepth = projected.z;

			float otherDepth = depthTextures[proj].SampleLevel(borderSampler, otherUV, 0);
			if (testDepth <= otherDepth + 0.00001f)
			{
				float otherIntensity = intensities[proj].SampleLevel(borderSampler, otherUV, 0);
				//float3 otherColor = renderResults[proj].SampleLevel(borderSampler, otherUV, 0).rgb;
				intensityByOtherProjectors += 1;// otherIntensity;
			}
		}
	}

	outCurrentIntensity[texCoord] = intensityByOtherProjectors;
}
