// Small depth-aware box blur of the SSAO result (input0, depth in input1).
// p0.xy: texel size.
#include "post_common.hlsli"

float4 Main(FullscreenOutput input) : SV_Target0
{
    float center_depth = input1.SampleLevel(point_clamp, input.uv, 0).r;
    float total = 0.0;
    float weight = 0.0;
    [unroll]
    for (int y = -2; y <= 2; ++y)
    {
        [unroll]
        for (int x = -2; x <= 2; ++x)
        {
            float2 uv = input.uv + float2(x, y) * params.p0.xy;
            float d = input1.SampleLevel(point_clamp, uv, 0).r;
            float w = abs(d - center_depth) < 0.002 ? 1.0 : 0.1;
            total += input0.SampleLevel(point_clamp, uv, 0).r * w;
            weight += w;
        }
    }
    return float4((total / weight).xxx, 1.0);
}
