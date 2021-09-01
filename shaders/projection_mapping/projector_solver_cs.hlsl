#include "cs.hlsli"
#include "projector_rs.hlsli"


struct projector_vp
{
    float4x4 viewProj;
};

ConstantBuffer<projector_solver_cb> cb  : register(b0, space0);
StructuredBuffer<projector_vp> vps      : register(t0, space0);

Texture2D<float4> renderResults[]       : register(t0, space1);
Texture2D<float> depthTextures[]        : register(t0, space2);
Texture2D<float> intensities[]          : register(t0, space3);

RWTexture2D<float> outCurrentIntensity  : register(u0, space0);


[RootSignature(PROJECTOR_SOLVER_RS)]
[numthreads(PROJECTOR_SOLVER_BLOCK_SIZE, PROJECTOR_SOLVER_BLOCK_SIZE, 1)]
void main(cs_input IN)
{
	uint2 texCoord = IN.dispatchThreadID.xy;
	if (texCoord.x >= cb.width || texCoord.y >= cb.height)
	{
		return;
	}

	uint index = cb.currentIndex;
	float depth = depthTextures[index][texCoord];

	outCurrentIntensity[texCoord] = 0.5f;
}
