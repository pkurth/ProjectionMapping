#include "cs.hlsli"
#include "projector_rs.hlsli"
#include "normal.hlsli"
#include "camera.hlsli"


struct projector_confidence_cb
{
	uint32 index;
	float referenceDistance;
	float desiredWhiteValue;
};


ConstantBuffer<projector_confidence_cb> cb	: register(b0, space0);
StructuredBuffer<projector_cb> projectors	: register(t0, space0);

Texture2D<float4> renderResults[32]			: register(t0, space1);
Texture2D<float2> worldNormals[32]			: register(t0, space2);
Texture2D<float> depthTextures[32]			: register(t0, space3);
Texture2D<float> intensities[32]			: register(t0, space4);
Texture2D<float> masks[32]					: register(t0, space5);

RWTexture2D<float> outConfidences[32]		: register(u0, space0);


[numthreads(PROJECTOR_BLOCK_SIZE, PROJECTOR_BLOCK_SIZE, 1)]
void main(cs_input IN)
{
	uint index = cb.index;

	uint2 texCoord = IN.dispatchThreadID.xy;
	float2 dimensions = projectors[index].screenDims;
	if (texCoord.x >= (uint)dimensions.x || texCoord.y >= (uint)dimensions.y)
	{
		return;
	}

	float depth = depthTextures[index][texCoord];
	if (depth == 1.f)
	{
		outConfidences[index][texCoord] = 0.f;
		return;
	}

	float2 invDimensions = projectors[index].invScreenDims;
	float2 uv = (float2(texCoord) + float2(0.5f, 0.5f)) * invDimensions;

	float4 color = renderResults[index][texCoord];
	float3 P = restoreWorldSpacePosition(projectors[index].invViewProj, uv, depth);
	float3 N = normalize(unpackNormal(worldNormals[index][texCoord]));
	float3 V = projectors[index].position.xyz - P;
	float distance = length(V);
	V *= rcp(distance);

	float possiblePhysicalIntensity = getAngleAttenuation(N, V) * getDistanceAttenuation(distance, cb.referenceDistance);

	float mask = masks[index][texCoord];

	outConfidences[index][texCoord] = saturate(possiblePhysicalIntensity / cb.desiredWhiteValue);
}
