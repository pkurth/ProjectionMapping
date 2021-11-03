#include "normal.hlsli"
#include "projector_rs.hlsli"
#include "camera.hlsli"
#include "color.hlsli"

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

Texture2D<float4> renderResults[]				: register(t0, space1);
Texture2D<float> depthTextures[]				: register(t0, space2);

SamplerState borderSampler						: register(s0);


[RootSignature(PROJECTOR_SIMULATION_RS)]
ps_output main(ps_input IN)
{
	uint numProjectors = cb.numProjectors;

	float3 color = float3(0.f, 0.f, 0.f);
	float3 N = normalize(IN.tbn[2]);

	for (uint projIndex = 0; projIndex < numProjectors; ++projIndex)
	{
		float4 projected = mul(projectors[projIndex].viewProj, float4(IN.worldPosition, 1.f));
		projected.xyz /= projected.w;

		float2 uv = projected.xy * 0.5f + float2(0.5f, 0.5f);
		uv.y = 1.f - uv.y;

		float3 V = projectors[projIndex].position.xyz - IN.worldPosition; // Not normalized.

		float depth = depthTextures[projIndex].SampleLevel(borderSampler, uv, 0);
		float eyeDepth = depthBufferDepthToEyeDepth(depth, projectors[projIndex].projectionParams);

		float testDepth = dot(projectors[projIndex].forward.xyz, -V);

		if (testDepth < eyeDepth + 0.001f)
		{
			float distance = length(V);
			V /= distance;
			
			float physicalIntensity = getAngleAttenuation(N, V) * getDistanceAttenuation(distance, cb.referenceDistance);

			float3 renderResult = renderResults[projIndex].SampleLevel(borderSampler, uv, 0).rgb; // Software intensity is baked into this already.
			renderResult = sRGBToLinear(renderResult);

			color += renderResult * physicalIntensity;
		}
	}

	ps_output OUT;

	OUT.hdrColor = float4(color, 0.f); // Alpha=0, so that no reflections are applied.
	OUT.worldNormal = packNormal(N);
	OUT.reflectance = float4(0.f, 0.f, 0.f, 0.f);

	return OUT;
}
