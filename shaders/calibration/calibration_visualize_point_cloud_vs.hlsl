#include "calibration_rs.hlsli"

ConstantBuffer<visualize_point_cloud_cb> cb	: register(b0);

struct vs_input
{
	float3 position : POSITION;
	float3 normal	: NORMAL;
};

struct vs_output
{
	float4 color	: COLOR;
	float4 position : SV_Position;
};

vs_output main(vs_input IN)
{
	vs_output OUT;
	OUT.position = mul(cb.mvp, float4(IN.position, 1.f));
	//OUT.normal = mul(cb.mv, float4(IN.normal, 0.f));
	OUT.color = cb.color;

	return OUT;
}
