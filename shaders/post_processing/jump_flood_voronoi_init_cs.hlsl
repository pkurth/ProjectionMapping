#include "cs.hlsli"
#include "post_processing_rs.hlsli"


ConstantBuffer<jump_flood_voronoi_init_cb> cb   : register(b0);
Texture2D<float2> input                         : register(t0);
RWTexture2D<float4> output                      : register(u0);


[numthreads(POST_PROCESSING_BLOCK_SIZE, POST_PROCESSING_BLOCK_SIZE, 1)]
[RootSignature(JUMP_FLOOD_VORONOI_INIT_RS)]
void main(cs_input IN)
{
    int2 tc = IN.dispatchThreadID.xy;

    float2 i = input[tc] * cb.inputMask;

    float4 o = float4(0.f, 0.f, 0.f, 0.f);

    float2 c = (float2)tc;

    if (i.x != 0.f)
    {
        o.xy = c;
    }
    if (i.y != 0.f)
    {
        o.zw = c;
    }

    output[tc] = o;
}

