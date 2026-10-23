// Bloom: tent upsample of the smaller mip (input0), blended over the
// current mip by the pipeline (additive, scaled by p0.z = scatter).
// p0.xy: source texel size.
#include "post_common.hlsli"

float4 Main(FullscreenOutput input) : SV_Target0
{
    float2 t = params.p0.xy;
    float2 uv = input.uv;
    float3 color = input0.SampleLevel(linear_clamp, uv + t * float2(-1, -1), 0).rgb * 1.0
                 + input0.SampleLevel(linear_clamp, uv + t * float2(0, -1), 0).rgb * 2.0
                 + input0.SampleLevel(linear_clamp, uv + t * float2(1, -1), 0).rgb * 1.0
                 + input0.SampleLevel(linear_clamp, uv + t * float2(-1, 0), 0).rgb * 2.0
                 + input0.SampleLevel(linear_clamp, uv, 0).rgb * 4.0
                 + input0.SampleLevel(linear_clamp, uv + t * float2(1, 0), 0).rgb * 2.0
                 + input0.SampleLevel(linear_clamp, uv + t * float2(-1, 1), 0).rgb * 1.0
                 + input0.SampleLevel(linear_clamp, uv + t * float2(0, 1), 0).rgb * 2.0
                 + input0.SampleLevel(linear_clamp, uv + t * float2(1, 1), 0).rgb * 1.0;
    return float4(color / 16.0 * params.p0.z, 1.0);
}
