#include "cs.hlsli"
#include "projector_rs.hlsli"
#include "brdf.hlsli"
#include "normal.hlsli"
#include "camera.hlsli"

ConstantBuffer<projector_specular_ambient_cb> cb	: register(b0);
ConstantBuffer<camera_cb> camera					: register(b1);

RWTexture2D<float4> output							: register(u0);

Texture2D<float4> scene								: register(t0);
Texture2D<float2> worldNormals						: register(t1);
Texture2D<float4> reflectance						: register(t2);
Texture2D<float> depthBuffer						: register(t3);


TextureCube<float4> environmentTexture				: register(t4);
Texture2D<float2> brdf								: register(t5);

SamplerState clampSampler							: register(s0);


[numthreads(PROJECTOR_BLOCK_SIZE, PROJECTOR_BLOCK_SIZE, 1)]
[RootSignature(PROJECTOR_SPECULAR_AMBIENT_RS)]
void main(cs_input IN)
{
	float2 uv = (IN.dispatchThreadID.xy + 0.5f) * cb.invDimensions;

	float4 color = scene[IN.dispatchThreadID.xy];
	if (color.a > 0.f) // Alpha of 0 indicates sky.
	{
		float4 refl = reflectance[IN.dispatchThreadID.xy];

		surface_info surface;
		surface.albedo = 0.f.xxxx;
		surface.metallic = 0.f; // Unused.
		surface.N = normalize(unpackNormal(worldNormals[IN.dispatchThreadID.xy]));
		surface.V = normalize(cb.viewerPosition.xyz - restoreWorldSpacePosition(camera.invViewProj, uv, depthBuffer[IN.dispatchThreadID.xy]));
		surface.roughness = clamp(refl.a, 0.01f, 0.99f);

		surface.inferRemainingProperties();

		float3 specular = specularIBL(refl.rgb, surface, environmentTexture, brdf, clampSampler);

		color.rgb += specular;
	}
	output[IN.dispatchThreadID.xy] = color;
}
