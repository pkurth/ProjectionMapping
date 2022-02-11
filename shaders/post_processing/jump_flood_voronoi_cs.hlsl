#include "cs.hlsli"
#include "post_processing_rs.hlsli"


ConstantBuffer<jump_flood_voronoi_cb> cb	: register(b0);
Texture2D<float4> seedTexture				: register(t0);
RWTexture2D<float4> output				    : register(u0);


[numthreads(POST_PROCESSING_BLOCK_SIZE, POST_PROCESSING_BLOCK_SIZE, 1)]
[RootSignature(JUMP_FLOOD_VORONOI_RS)]
void main(cs_input IN)
{
	int2 tc = IN.dispatchThreadID.xy;

    float2 bestDist = (float2)999999.f;
    float4 bestCoord = (float4)0.f;
    float2 center = float2(tc);

    for (int y = -1; y <= 1; ++y) 
    {
        for (int x = -1; x <= 1; ++x) 
        {
            int2 fc = tc + int2(x, y) * cb.stepWidth;
            float4 ntc = seedTexture[fc];

            float4 o = ntc - center.xyxy;

            {
                float d = dot(o.xy, o.xy);
                if ((ntc.x != 0.f) && (ntc.y != 0.f) && (d < bestDist.x))
                {
                    bestDist.x = d;
                    bestCoord.xy = ntc.xy;
                }
            }

            {
                float d = dot(o.zw, o.zw);
                if ((ntc.z != 0.f) && (ntc.w != 0.f) && (d < bestDist.y))
                {
                    bestDist.y = d;
                    bestCoord.zw = ntc.zw;
                }
            }
        }
    }

    output[tc] = bestCoord;
}
