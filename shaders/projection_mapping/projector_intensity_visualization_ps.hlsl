#include "normal.hlsli"
#include "projector_rs.hlsli"

struct ps_input
{
	float2 uv				: TEXCOORDS;
	float3x3 tbn			: TANGENT_FRAME;
	float3 worldPosition	: POSITION;

	float4 screenPosition	: SV_POSITION;
	bool isFrontFace		: SV_IsFrontFace;
};

struct ps_output
{
	float4 hdrColor		: SV_Target0;
	float2 worldNormal	: SV_Target1;
	float4 reflectance	: SV_Target2;
};

ConstantBuffer<projector_visualization_cb> cb	: register(b0, space1);

StructuredBuffer<projector_cb> projectors		: register(t0, space0);
Texture2D<float> depthTextures[]				: register(t0, space1);
Texture2D<float> intensities[]					: register(t0, space2);

SamplerState borderSampler						: register(s0);


static const float3 colorTable[] =
{
	float3(1.f, 0.f, 0.f),
	float3(0.f, 1.f, 0.f),
	float3(0.f, 0.f, 1.f),
	float3(1.f, 1.f, 0.f),
	float3(0.f, 1.f, 1.f),
	float3(1.f, 0.f, 1.f),
};

[RootSignature(PROJECTOR_INTENSITY_VISUALIZATION_RS)]
ps_output main(ps_input IN)
{
	uint numProjectors = cb.numProjectors;
	float3 P = IN.worldPosition;
	float3 N = IN.tbn[2];

	float3 color = float3(0.f, 0.f, 0.f);

	for (uint projIndex = 0; projIndex < numProjectors; ++projIndex)
	{
		float4 projected = mul(projectors[projIndex].viewProj, float4(P, 1.f));
		projected.xyz /= projected.w;

		float2 uv = projected.xy * 0.5f + float2(0.5f, 0.5f);
		uv.y = 1.f - uv.y;
		float testDepth = projected.z;

		float depth = depthTextures[projIndex].SampleLevel(borderSampler, uv, 0);
		if (testDepth <= depth + 0.00001f)
		{
			float3 V = normalize(projectors[projIndex].position.xyz - P.xyz);

			float NdotV = saturate(dot(N, V)); // Physical intensity.
			float intensity = intensities[projIndex].SampleLevel(borderSampler, uv, 0); // Software intensity.

			float totalIntensity = NdotV * intensity;
			color += totalIntensity * colorTable[projIndex];
		}
	}


	ps_output OUT;

	OUT.hdrColor = float4(color, 0.f); // Alpha=0, so that no reflections are applied.
	OUT.worldNormal = packNormal(N);
	OUT.reflectance = float4(0.f, 0.f, 0.f, 0.f);

	return OUT;
}
