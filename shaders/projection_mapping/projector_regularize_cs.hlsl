#include "cs.hlsli"
#include "projector_rs.hlsli"


ConstantBuffer<projector_regularize_cb> cb	: register(b0, space0);

Texture2D<float> intensities[32]			: register(t0, space1);
Texture2D<float> depthTextures[32]			: register(t0, space2);
Texture2D<float> masks[32]					: register(t0, space3);
RWTexture2D<float> outIntensities[32]		: register(u0, space0);

[RootSignature(PROJECTOR_REGULARIZE_RS)]
[numthreads(PROJECTOR_BLOCK_SIZE, PROJECTOR_BLOCK_SIZE, 1)]
void main(cs_input IN)
{
	uint2 texCoord = IN.dispatchThreadID.xy;

	uint index = cb.currentIndex;
	float strength = cb.strength;// *(1.f - masks[index][texCoord]);

	float left = intensities[index][texCoord - uint2(1, 0)];
	float top = intensities[index][texCoord - uint2(0, 1)];
	float right = intensities[index][texCoord + uint2(1, 0)];
	float bottom = intensities[index][texCoord + uint2(0, 1)];
	float center = intensities[index][texCoord];

	float regularized = 0.25f * (left + top + right + bottom);
	outIntensities[index][texCoord] = lerp(center, regularized, strength);
}

