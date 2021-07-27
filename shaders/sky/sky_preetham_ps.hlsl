#include "math.hlsli"
#include "sky_rs.hlsli"
#include "color.hlsli"

// From https://www.shadertoy.com/view/llSSDR.

ConstantBuffer<sky_cb> sky : register(b1);

struct ps_input
{
	float3 uv		: TEXCOORDS;
};

struct ps_output
{
	float4 color			: SV_Target0;
	float2 screenVelocity	: SV_Target1;
	uint objectID			: SV_Target2;
};

static void calculatePerezDistribution(float t, out float3 A, out float3 B, out float3 C, out float3 D, out float3 E)
{
	A = float3(0.1787f * t - 1.4630f, -0.0193f * t - 0.2592f, -0.0167f * t - 0.2608f);
	B = float3(-0.3554f * t + 0.4275f, -0.0665f * t + 0.0008f, -0.0950f * t + 0.0092f);
	C = float3(-0.0227f * t + 5.3251f, -0.0004f * t + 0.2125f, -0.0079f * t + 0.2102f);
	D = float3(0.1206f * t - 2.5771f, -0.0641f * t - 0.8989f, -0.0441f * t - 1.6537f);
	E = float3(-0.0670f * t + 0.3703f, -0.0033f * t + 0.0452f, -0.0109f * t + 0.0529f);
}

static float3 calculateZenithLuminanceYxy(float t, float thetaS)
{
	float chi = (4.f / 9.f - t / 120.f) * (pi - 2.f * thetaS);
	float Yz = (4.0453f * t - 4.9710f) * tan(chi) - 0.2155f * t + 2.4192f;

	float theta2 = thetaS * thetaS;
	float theta3 = theta2 * thetaS;
	float T = t;
	float T2 = t * t;

	float xz =
		(0.00165f * theta3 - 0.00375f * theta2 + 0.00209f * thetaS + 0.f) * T2 +
		(-0.02903f * theta3 + 0.06377f * theta2 - 0.03202f * thetaS + 0.00394f) * T +
		(0.11693f * theta3 - 0.21196f * theta2 + 0.06052f * thetaS + 0.25886f);

	float yz =
		(0.00275f * theta3 - 0.00610f * theta2 + 0.00317f * thetaS + 0.f) * T2 +
		(-0.04214f * theta3 + 0.08970f * theta2 - 0.04153f * thetaS + 0.00516f) * T +
		(0.15346f * theta3 - 0.26756f * theta2 + 0.06670f * thetaS + 0.26688f);

	return float3(Yz, xz, yz);
}

static float3 calculatePerezLuminanceYxy(float cosTheta, float gamma, float cosGamma, float3 A, float3 B, float3 C, float3 D, float3 E)
{
	return (1.f + A * exp(B / cosTheta)) * (1.f + C * exp(D * gamma) + E * cosGamma * cosGamma);
}

static float3 calculateSkyLuminanceRGB(float3 s, float3 e, float t)
{
	float3 A, B, C, D, E;
	calculatePerezDistribution(t, A, B, C, D, E);

	float cosThetaE = saturate(e.y);

	float cosThetaS = saturate(s.y);
	float thetaS = acos(cosThetaS);

	float cosGammaE = saturate(dot(s, e));
	float gammaE = acos(cosGammaE);

	float3 Yz = calculateZenithLuminanceYxy(t, thetaS);

	float3 thetaGamma = calculatePerezLuminanceYxy(cosThetaE, gammaE, cosGammaE, A, B, C, D, E);
	float3 zeroThetaS = calculatePerezLuminanceYxy(1.f, thetaS, cosThetaS, A, B, C, D, E);

	float3 Yp = Yz * (thetaGamma / zeroThetaS);

	return YxyToRGB(Yp);
}

[RootSignature(SKY_PREETHAM_RS)]
ps_output main(ps_input IN)
{
	const float turbidity = 2.f;

	float3 V = normalize(IN.uv);
	float3 skyLuminance = max(calculateSkyLuminanceRGB(-sky.sunDirection, V, turbidity), 0.f.xxx);

	ps_output OUT;
	OUT.color = float4(skyLuminance * 0.08f * sky.intensity, 0.f);
	OUT.screenVelocity = float2(0.f, 0.f); // TODO: This is of course not the correct screen velocity for the sky.
	OUT.objectID = 0xFFFFFFFF; // -1.
	return OUT;
}
